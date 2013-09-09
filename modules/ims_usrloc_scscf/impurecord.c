/*
 * $Id$
 *
 * Copyright (C) 2012 Smile Communications, jason.penton@smilecoms.com
 * Copyright (C) 2012 Smile Communications, richard.good@smilecoms.com
 * 
 * The initial version of this code was written by Dragos Vingarzan
 * (dragos(dot)vingarzan(at)fokus(dot)fraunhofer(dot)de and the
 * Fruanhofer Institute. It was and still is maintained in a separate
 * branch of the original SER. We are therefore migrating it to
 * Kamailio/SR and look forward to maintaining it from here on out.
 * 2011/2012 Smile Communications, Pty. Ltd.
 * ported/maintained/improved by 
 * Jason Penton (jason(dot)penton(at)smilecoms.com and
 * Richard Good (richard(dot)good(at)smilecoms.com) as part of an 
 * effort to add full IMS support to Kamailio/SR using a new and
 * improved architecture
 * 
 * NB: Alot of this code was originally part of OpenIMSCore,
 * FhG Fokus. 
 * Copyright (C) 2004-2006 FhG Fokus
 * Thanks for great work! This is an effort to 
 * break apart the various CSCF functions into logically separate
 * components. We hope this will drive wider use. We also feel
 * that in this way the architecture is more complete and thereby easier
 * to manage in the Kamailio/SR environment
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * Kamailio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * Kamailio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */

#include "impurecord.h"
#include <string.h>
#include "../../hashes.h"
#include "../../mem/shm_mem.h"
#include "../../dprint.h"
#include "../../ut.h"
#include "ul_mod.h"
#include "usrloc.h"
#include "utime.h"
#include "ul_callback.h"
#include "usrloc.h"
#include "bin_utils.h"
#include "subscribe.h"
#include "../../lib/ims/useful_defs.h"

/*! contact matching mode */
int matching_mode = CONTACT_ONLY;
/*! retransmission detection interval in seconds */
int cseq_delay = 20;

extern int unreg_validity;
extern int maxcontact_behaviour;
extern int maxcontact;

/*!
 * \brief Create and initialize new record structure
 * \param _dom domain name
 * \param _aor address of record
 * \param _r pointer to the new record
 * \return 0 on success, negative on failure
 */
int new_impurecord(str* _dom, str* public_identity, int reg_state, int barring, ims_subscription** s, str* ccf1, str* ccf2, str* ecf1, str* ecf2, impurecord_t** _r) {
    *_r = (impurecord_t*) shm_malloc(sizeof (impurecord_t));
    if (*_r == 0) {
        LM_ERR("no more shared memory\n");
        return -1;
    }
    memset(*_r, 0, sizeof (impurecord_t));

    //setup callback list
    (*_r)->cbs = (struct ulcb_head_list*) shm_malloc(
            sizeof (struct ulcb_head_list));
    if ((*_r)->cbs == 0) {
        LM_CRIT("no more shared mem\n");
        shm_free(*_r);
        *_r = 0;
        return -2;
    }
    (*_r)->cbs->first = 0;
    (*_r)->cbs->reg_types = 0;

    (*_r)->public_identity.s = (char*) shm_malloc(public_identity->len);
    if ((*_r)->public_identity.s == 0) {
        LM_ERR("no more shared memory\n");
        shm_free(*_r);
        *_r = 0;
        return -2;
    }
    memcpy((*_r)->public_identity.s, public_identity->s, public_identity->len);
    (*_r)->public_identity.len = public_identity->len;
    (*_r)->domain = _dom;
    (*_r)->aorhash = core_hash(public_identity, 0, 0);
    (*_r)->reg_state = reg_state;
    if (barring >= 0) { //just in case we call this with no barring -1 will ignore
        (*_r)->barring = barring;
    }
    if (ccf1 && ccf1->len > 0) STR_SHM_DUP((*_r)->ccf1, *ccf1, "CCF1");
    if (ccf2 && ccf2->len > 0) STR_SHM_DUP((*_r)->ccf2, *ccf2, "CCF2");
    if (ecf1 && ecf1->len > 0) STR_SHM_DUP((*_r)->ecf1, *ecf1, "ECF1");
    if (ecf2 && ecf2->len > 0) STR_SHM_DUP((*_r)->ecf2, *ecf2, "ECF2");
    /*assign ims subscription profile*/
    if (*s) {
        (*_r)->s = *s;
        lock_get((*_r)->s->lock);
        (*_r)->s->ref_count++;
        lock_release((*_r)->s->lock);
    }

    return 0;

out_of_memory:
    LM_ERR("no more shared memory\n");
    return -3;
}

/*!
 * \brief Free all memory used by the given structure
 *
 * Free all memory used by the given structure.
 * The structure must be removed from all linked
 * lists first
 * \param _r freed record list
 */
void free_impurecord(impurecord_t* _r) {
    ucontact_t* ptr;
    struct ul_callback *cbp, *cbp_tmp;
    struct _reg_subscriber* subscriber, *s_tmp;

    while (_r->contacts) {
        ptr = _r->contacts;
        _r->contacts = _r->contacts->next;
        free_ucontact(ptr);
    }

    //free IMS specific extensions
    if (_r->ccf1.s)
        shm_free(_r->ccf1.s);
    if (_r->ccf2.s)
        shm_free(_r->ccf2.s);
    if (_r->ecf1.s)
        shm_free(_r->ecf1.s);
    if (_r->ecf2.s)
        shm_free(_r->ecf2.s);
    if (_r->s) {
        LM_DBG("ref count on this IMS data is %d\n", _r->s->ref_count);
        lock_get(_r->s->lock);
        if (_r->s->ref_count == 1) {
            LM_DBG("freeing IMS subscription data\n");
            free_ims_subscription_data(_r->s);
        } else {
            LM_DBG("decrementing IMS subscription data ref count\n");
            _r->s->ref_count--;
            lock_release(_r->s->lock);
        }
    }

    /*remove REG subscriptions to this IMPU*/
    subscriber = _r->shead;
    while (subscriber) {
        s_tmp = subscriber->next;
        free_subscriber(subscriber);
        subscriber = s_tmp;
    }

    if (_r->public_identity.s)
        shm_free(_r->public_identity.s);

    //free callback list
    for (cbp = _r->cbs->first; cbp;) {
        cbp_tmp = cbp;
        cbp = cbp->next;
        if (cbp_tmp->param)
            shm_free(cbp_tmp->param);
        shm_free(cbp_tmp);
    }
    shm_free(_r->cbs);


    shm_free(_r);
}

/*!
 * \brief Print a record, useful for debugging
 * \param _f print output
 * \param _r printed record
 */
void print_impurecord(FILE* _f, impurecord_t* _r) {
    ucontact_t* ptr;

    fprintf(_f, "...Record(%p)...\n", _r);
    fprintf(_f, "domain : '%.*s'\n", _r->domain->len, ZSW(_r->domain->s));
    fprintf(_f, "public_identity    : '%.*s'\n", _r->public_identity.len, ZSW(_r->public_identity.s));
    fprintf(_f, "aorhash: '%u'\n", (unsigned) _r->aorhash);
    fprintf(_f, "slot:    '%d'\n", _r->aorhash & (_r->slot->d->size - 1));
    fprintf(_f, "pi_ref:  '%d'\n", _r->reg_state);
    fprintf(_f, "barring: '%d'\n", _r->barring);
    fprintf(_f, "ccf1:    '%.*s'\n", _r->ccf1.len, _r->ccf1.s);
    fprintf(_f, "ccf2:    '%.*s'\n", _r->ccf2.len, _r->ccf2.s);
    fprintf(_f, "ecf1:    '%.*s'\n", _r->ecf1.len, _r->ecf1.s);
    fprintf(_f, "ecf2:    '%.*s'\n", _r->ecf2.len, _r->ecf2.s);
    if (_r->s) {
        fprintf(_f, "IMS subs (#%d):   '%p'\n", _r->s->service_profiles_cnt, _r->s);
        fprintf(_f, "#profiles: '%d\n", _r->s->service_profiles_cnt);
    }

    int header = 0;
    reg_subscriber* subscriber = _r->shead;
    while (subscriber) {
        if (!header) {
            fprintf(_f, "...Subscriptions...\n");
            header = 1;
        }
        fprintf(_f, "watcher uri: <%.*s> and presentity uri: <%.*s>\n", subscriber->watcher_uri.len, subscriber->watcher_uri.s, subscriber->presentity_uri.len, subscriber->presentity_uri.s);
        fprintf(_f, "Expires: %ld\n", subscriber->expires);
        subscriber = subscriber->next;
    }

    if (_r->contacts) {
        ptr = _r->contacts;
        while (ptr) {
            print_ucontact(_f, ptr);
            ptr = ptr->next;
        }
    }

    fprintf(_f, ".../Record...\n\n\n\n");
}

/*!
 * \brief Add a new contact in memory
 *
 * Add a new contact in memory, contacts are ordered by:
 * 1) q value, 2) descending modification time
 * \param _r record this contact belongs to
 * \param _c contact
 * \param _ci contact information
 * \return pointer to new created contact on success, 0 on failure
 */
ucontact_t* mem_insert_ucontact(impurecord_t* _r, str* _c, ucontact_info_t* _ci) {
    ucontact_t* ptr, *prev = 0;
    ucontact_t* c;

    if ((c = new_ucontact(_r->domain, &_r->public_identity, _c, _ci)) == 0) {
        LM_ERR("failed to create new contact\n");
        return 0;
    }
    if_update_stat(_r->slot, _r->slot->d->contacts, 1);

    ptr = _r->contacts;

    while (ptr) {//make sure our contacts are ordered oldest(first) to newest(last)
        if (ptr->expires > c->expires)
            break;
        prev = ptr;
        ptr = ptr->next;
    }

    if (ptr) {
        if (!ptr->prev) {
            ptr->prev = c;
            c->next = ptr;
            _r->contacts = c;
        } else {
            c->next = ptr;
            c->prev = ptr->prev;
            ptr->prev->next = c;
            ptr->prev = c;
        }
    } else if (prev) {
        prev->next = c;
        c->prev = prev;
    } else {
        _r->contacts = c;
    }

    return c;
}

/*!
 * \brief Remove the contact from lists in memory
 * \param _r record this contact belongs to
 * \param _c removed contact
 */
void mem_remove_ucontact(impurecord_t* _r, ucontact_t* _c) {
    if (_c->prev) {
        _c->prev->next = _c->next;
        if (_c->next) {
            _c->next->prev = _c->prev;
        }
    } else {
        _r->contacts = _c->next;
        if (_c->next) {
            _c->next->prev = 0;
        }
    }
}

/*!
 * \brief Remove contact in memory from the list and delete it
 * \param _r record this contact belongs to
 * \param _c deleted contact
 */
void mem_delete_ucontact(impurecord_t* _r, ucontact_t* _c) {
    mem_remove_ucontact(_r, _c);
    if_update_stat(_r->slot, _r->slot->d->contacts, -1);
    free_ucontact(_c);
}

/*!
 * \brief Expires timer for NO_DB db_mode
 *
 * Expires timer for NO_DB db_mode, process all contacts from
 * the record, delete the expired ones from memory.
 * \param _r processed record
 */
static inline void nodb_timer(impurecord_t* _r) {
    ucontact_t* ptr, *t;

    reg_subscriber *s;

    get_act_time();

    s = _r->shead;
    LM_DBG("Checking validity of IMPU: <%.*s> registration subscriptions\n", _r->public_identity.len, _r->public_identity.s);
    while (s) {
        if (!valid_subscriber(s)) {
            LM_DBG("DBG:registrar_timer: Subscriber with watcher_contact <%.*s> and presentity uri <%.*s> expired and removed.\n",
                    s->watcher_contact.len, s->watcher_contact.s, s->presentity_uri.len, s->presentity_uri.s);
            delete_subscriber(_r, s);
        } else {
            LM_DBG("DBG:registrar_timer: Subscriber with watcher_contact <%.*s> and presentity uri <%.*s> is valid and expires in %d seconds.\n",
                    s->watcher_contact.len, s->watcher_contact.s, s->presentity_uri.len, s->presentity_uri.s,
                    (unsigned int) (s->expires - time(NULL)));
        }
        s = s->next;
    }

    ptr = _r->contacts;
    LM_DBG("Checking validity of IMPU: <%.*s> contacts\n", _r->public_identity.len, _r->public_identity.s);

    while (ptr) {
        if (!VALID_CONTACT(ptr, act_time)) {
            /* run callbacks for EXPIRE event */
            if (exists_ulcb_type(ptr->cbs, UL_CONTACT_EXPIRE))
                run_ul_callbacks(ptr->cbs, UL_CONTACT_EXPIRE, _r, ptr);

            if (exists_ulcb_type(_r->cbs, UL_IMPU_EXPIRE_CONTACT)) {
                run_ul_callbacks(_r->cbs, UL_IMPU_EXPIRE_CONTACT, _r, ptr);
            }

            LM_DBG("Binding '%.*s','%.*s' has expired\n",
                    ptr->aor->len, ZSW(ptr->aor->s),
                    ptr->c.len, ZSW(ptr->c.s));

            t = ptr;
            ptr = ptr->next;

            mem_delete_ucontact(_r, t);
            update_stat(_r->slot->d->expires, 1);
        } else {
            LM_DBG("IMPU:<%.*s> - contact:<%.*s> is valid and expires in %d seconds\n", _r->public_identity.len, _r->public_identity.s,
                    ptr->c.len, ptr->c.s,
                    (unsigned int) (ptr->expires - time(NULL)));
            ptr = ptr->next;
        }
    }
}

/*!
 * \brief Run timer functions depending on the db_mode setting.
 *
 * Helper function that run the appropriate timer function, depending
 * on the db_mode setting.
 * \param _r processed record
 */
void timer_impurecord(impurecord_t* _r) {
    nodb_timer(_r);
}

/*!
 * \brief Create and insert new contact into impurecord
 * \param _r record into the new contact should be inserted
 * \param _contact contact string
 * \param _ci contact information
 * \param _c new created contact
 * \return 0 on success, -1 on failure
 */
int insert_ucontact(impurecord_t* _r, str* _contact, ucontact_info_t* _ci, ucontact_t** _c) {
    //First check our constraints
    if (maxcontact > 0 && maxcontact_behaviour > 0) {
        ucontact_t* contact = _r->contacts;
        int numcontacts = 0;
        while (contact) {
            numcontacts++;
            contact = contact->next;
        }
        if (numcontacts >= maxcontact) {
            switch (maxcontact_behaviour) {
                case 1://reject
                    LM_ERR("too many contacts already registered for IMPU <%.*s>\n", _r->public_identity.len, _r->public_identity.s);
                    return -1;
                case 2://overwrite oldest
                    LM_DBG("Too many contacts already registered, overwriting oldest for IMPU <%.*s>\n", _r->public_identity.len, _r->public_identity.s);
                    //we can just remove the first one seeing the contacts are ordered on insertion with newest last and oldest first
                    mem_delete_ucontact(_r, _r->contacts);
                    break;
                default://unknown
                    LM_ERR("unknown maxcontact behaviour..... ignoring\n");
                    break;
            }
        }
    }

    //at this stage we are safe to insert the new contact
    LM_DBG("INSERTing ucontact in usrloc module\n");
    if (((*_c) = mem_insert_ucontact(_r, _contact, _ci)) == 0) {
        LM_ERR("failed to insert contact\n");
        return -1;
    }

    if (exists_ulcb_type(NULL, UL_CONTACT_INSERT)) {
        run_ul_callbacks(NULL, UL_CONTACT_INSERT, _r, *_c);
    }
    if (exists_ulcb_type(_r->cbs, UL_IMPU_NEW_CONTACT)) {
        run_ul_callbacks(_r->cbs, UL_IMPU_NEW_CONTACT, _r, *_c);
    }

    return 0;
}

/*!
 * \brief Delete ucontact from impurecord
 * \param _r record where the contact belongs to
 * \param _c deleted contact
 * \return 0 on success, -1 on failure
 */
int delete_ucontact(impurecord_t* _r, struct ucontact* _c) {
    int ret = 0;
    reg_subscriber *s;

    //Richard added this  - fix to remove subscribes that have presentity and watcher uri same as a contact aor that is being removed
    s = _r->shead;
    LM_DBG("Checking if there is a subscription to this IMPU that has same watcher contact as this contact");
    while (s) {
        
        LM_DBG("Subscription for this impurecord: watcher uri [%.*s] presentity uri [%.*s] watcher contact [%.*s] ", s->watcher_uri.len, s->watcher_uri.s, 
                s->presentity_uri.len, s->presentity_uri.s, s->watcher_contact.len, s->watcher_contact.s);
        LM_DBG("Contact to be removed [%.*s] ", _c->c.len, _c->c.s);
        if ((s->watcher_contact.len == _c->c.len) && (strncasecmp(s->watcher_contact.s, _c->c.s, _c->c.len) == 0)) {
            LM_DBG("This contact has a subscription to its own status - so going to delete the subscription");
            delete_subscriber(_r, s);
        }
        s = s->next;
    }
    
    if (exists_ulcb_type(_c->cbs, UL_CONTACT_DELETE)) {
        run_ul_callbacks(_c->cbs, UL_CONTACT_DELETE, _r, _c);
    }
    if (exists_ulcb_type(_r->cbs, UL_IMPU_DELETE_CONTACT)) {
        run_ul_callbacks(_r->cbs, UL_IMPU_DELETE_CONTACT, _r, _c);
    }

    

    mem_delete_ucontact(_r, _c);

    return ret;
}

/*!
 * \brief Match a contact record to a contact string
 * \param ptr contact record
 * \param _c contact string
 * \return ptr on successfull match, 0 when they not match
 */
static inline struct ucontact* contact_match(ucontact_t* ptr, str* _c) {
    while (ptr) {
        if ((_c->len == ptr->c.len) && !memcmp(_c->s, ptr->c.s, _c->len)) {
            return ptr;
        }

        ptr = ptr->next;
    }
    return 0;
}

/*!
 * \brief Match a contact record to a contact string and callid
 * \param ptr contact record
 * \param _c contact string
 * \param _callid callid
 * \return ptr on successfull match, 0 when they not match
 */
static inline struct ucontact* contact_callid_match(ucontact_t* ptr,
        str* _c, str *_callid) {
    while (ptr) {
        if ((_c->len == ptr->c.len) && (_callid->len == ptr->callid.len)
                && !memcmp(_c->s, ptr->c.s, _c->len)
                && !memcmp(_callid->s, ptr->callid.s, _callid->len)
                ) {
            return ptr;
        }

        ptr = ptr->next;
    }
    return 0;
}

/*!
+ * \brief Match a contact record to a contact string and path
+ * \param ptr contact record
+ * \param _c contact string
+ * \param _path path
+ * \return ptr on successfull match, 0 when they not match
+ */
static inline struct ucontact* contact_path_match(ucontact_t* ptr, str* _c, str *_path) {
    /* if no path is preset (in REGISTER request) or use_path is not configured
       in registrar module, default to contact_match() */
    if (_path == NULL) return contact_match(ptr, _c);

    while (ptr) {
        if ((_c->len == ptr->c.len) && (_path->len == ptr->path.len)
                && !memcmp(_c->s, ptr->c.s, _c->len)
                && !memcmp(_path->s, ptr->path.s, _path->len)
                ) {
            return ptr;
        }

        ptr = ptr->next;
    }
    return 0;
}

/*!
 * \brief Get pointer to ucontact with given contact
 * \param _r record where to search the contacts
 * \param _c contact string
 * \param _callid callid
 * \param _path path
 * \param _cseq CSEQ number
 * \param _co found contact
 * \return 0 - found, 1 - not found, -1 - invalid found,
 * -2 - found, but to be skipped (same cseq)
 */
int get_ucontact(impurecord_t* _r, str* _c, str* _callid, str* _path, int _cseq, struct ucontact** _co) {
    ucontact_t* ptr;
    int no_callid;

    ptr = 0;
    no_callid = 0;
    *_co = 0;

    switch (matching_mode) {
        case CONTACT_ONLY:
            ptr = contact_match(_r->contacts, _c);
            break;
        case CONTACT_CALLID:
            ptr = contact_callid_match(_r->contacts, _c, _callid);
            no_callid = 1;
            break;
        case CONTACT_PATH:
            ptr = contact_path_match(_r->contacts, _c, _path);
            break;
        default:
            LM_CRIT("unknown matching_mode %d\n", matching_mode);
            return -1;
    }

    if (ptr) {
        /* found -> check callid and cseq */
        if (no_callid || (ptr->callid.len == _callid->len
                && memcmp(_callid->s, ptr->callid.s, _callid->len) == 0)) {
            if (_cseq < ptr->cseq)
                return -1;
            if (_cseq == ptr->cseq) {
                get_act_time();
                return (ptr->last_modified + cseq_delay > act_time) ? -2 : -1;
            }
        }
        *_co = ptr;
        return 0;
    }

    return 1;
}

/**
 * Deallocates memory used by a subscription.
 * \note Must be called with the lock got to avoid races
 * @param s - the ims_subscription to free
 */
void free_ims_subscription_data(ims_subscription *s) {
    int i, j, k;
    if (!s) return;
    /*	lock_get(s->lock); - must be called with the lock got */
    for (i = 0; i < s->service_profiles_cnt; i++) {
        for (j = 0; j < s->service_profiles[i].public_identities_cnt; j++) {
            if (s->service_profiles[i].public_identities[j].public_identity.s)
                shm_free(s->service_profiles[i].public_identities[j].public_identity.s);
            if (s->service_profiles[i].public_identities[j].wildcarded_psi.s)
                shm_free(s->service_profiles[i].public_identities[j].wildcarded_psi.s);

        }
        if (s->service_profiles[i].public_identities)
            shm_free(s->service_profiles[i].public_identities);

        for (j = 0; j < s->service_profiles[i].filter_criteria_cnt; j++) {
            if (s->service_profiles[i].filter_criteria[j].trigger_point) {
                for (k = 0; k < s->service_profiles[i].filter_criteria[j].trigger_point->spt_cnt; k++) {
                    switch (s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].type) {
                        case IFC_REQUEST_URI:
                            if (s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].request_uri.s)
                                shm_free(s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].request_uri.s);
                            break;
                        case IFC_METHOD:
                            if (s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].method.s)
                                shm_free(s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].method.s);
                            break;
                        case IFC_SIP_HEADER:
                            if (s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].sip_header.header.s)
                                shm_free(s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].sip_header.header.s);
                            if (s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].sip_header.content.s)
                                shm_free(s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].sip_header.content.s);
                            break;
                        case IFC_SESSION_CASE:
                            break;
                        case IFC_SESSION_DESC:
                            if (s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].session_desc.line.s)
                                shm_free(s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].session_desc.line.s);
                            if (s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].session_desc.content.s)
                                shm_free(s->service_profiles[i].filter_criteria[j].trigger_point->spt[k].session_desc.content.s);
                            break;

                    }
                }
                if (s->service_profiles[i].filter_criteria[j].trigger_point->spt)
                    shm_free(s->service_profiles[i].filter_criteria[j].trigger_point->spt);
                shm_free(s->service_profiles[i].filter_criteria[j].trigger_point);
            }
            if (s->service_profiles[i].filter_criteria[j].application_server.server_name.s)
                shm_free(s->service_profiles[i].filter_criteria[j].application_server.server_name.s);
            if (s->service_profiles[i].filter_criteria[j].application_server.service_info.s)
                shm_free(s->service_profiles[i].filter_criteria[j].application_server.service_info.s);
            if (s->service_profiles[i].filter_criteria[j].profile_part_indicator)
                shm_free(s->service_profiles[i].filter_criteria[j].profile_part_indicator);
        }
        if (s->service_profiles[i].filter_criteria)
            shm_free(s->service_profiles[i].filter_criteria);

        if (s->service_profiles[i].cn_service_auth)
            shm_free(s->service_profiles[i].cn_service_auth);

        if (s->service_profiles[i].shared_ifc_set)
            shm_free(s->service_profiles[i].shared_ifc_set);
    }
    if (s->service_profiles) shm_free(s->service_profiles);
    if (s->private_identity.s) shm_free(s->private_identity.s);
    lock_destroy(s->lock);
    lock_dealloc(s->lock);
    shm_free(s);

}

/* update an existing impurecord. if one doesnt exist it will be created.
 * make sure yuo lock the domain before calling this and unlock it afterwards
 * return: 0 on success, -1 on failure
 */
int update_impurecord(struct udomain* _d, str* public_identity, int reg_state, int barring, int is_primary, ims_subscription** s, str* ccf1, str* ccf2, str* ecf1, str* ecf2, struct impurecord** _r) {
    int res;

    res = get_impurecord(_d, public_identity, _r);
    if (res != 0) {
        if (reg_state != IMPU_NOT_REGISTERED && s) {
            LM_DBG("No existing impu record for <%.*s>.... creating new one\n", public_identity->len, public_identity->s);
            res = insert_impurecord(_d, public_identity, reg_state, barring, s, ccf1, ccf2, ecf1, ecf2, _r);

            //for the first time we create an IMPU we must set the primary record (we don't worry about it on updates - ignored)
            (*_r)->is_primary = is_primary; //TODO = this should prob move to insert_impurecord fn

            if (reg_state == IMPU_UNREGISTERED) {
                //update unreg expiry so the unreg record is not stored 'forever'
                (*_r)->expires = time(NULL) + unreg_validity;
            }
            if (res != 0) {
                LM_ERR("Unable to insert new IMPU for <%.*s>\n", public_identity->len, public_identity->s);
                return -1;
            } else {
                run_ul_callbacks(NULL, UL_IMPU_INSERT, *_r, NULL);
                return 0;
            }
        } else {
            LM_ERR("no IMPU found to update and data not valid to create new one\n");
            return -1;
        }

    }

    //if we get here, we have a record to update
    LM_DBG("updating IMPU record with public identity for <%.*s>\n", public_identity->len, public_identity->s);
    (*_r)->reg_state = reg_state;
    if (reg_state == IMPU_UNREGISTERED) {
        //update unreg expiry so the unreg record is not stored 'forever'
        (*_r)->expires = time(NULL) + unreg_validity;
    }
    if (barring >= 0) (*_r)->barring = barring;
    if (ccf1) {
        if ((*_r)->ccf1.s)
            shm_free((*_r)->ccf1.s);
        STR_SHM_DUP((*_r)->ccf1, *ccf1, "SHM CCF1");
    }
    if (ccf2) {
        if ((*_r)->ccf2.s)
            shm_free((*_r)->ccf2.s);
        STR_SHM_DUP((*_r)->ccf2, *ccf2, "SHM CCF2");
    }
    if (ecf1) {
        if ((*_r)->ecf1.s)
            shm_free((*_r)->ecf1.s);
        STR_SHM_DUP((*_r)->ecf1, *ecf1, "SHM ECF1");
    }
    if (ecf2) {
        if ((*_r)->ecf2.s)
            shm_free((*_r)->ecf2.s);
        STR_SHM_DUP((*_r)->ecf2, *ecf2, "SHM ECF2");
    }

    if (s) {
        LM_DBG("we have a new ims_subscription\n");
        if ((*_r)->s) {
            lock_get((*_r)->s->lock);
            if ((*_r)->s->ref_count == 1) {
                LM_DBG("freeing user data as no longer referenced\n");
                free_ims_subscription_data((*_r)->s); //no need to release lock after this. its gone ;)
                (*_r)->s = 0;
            } else {
                (*_r)->s->ref_count--;
                LM_DBG("new ref count for ims sub is %d\n", (*_r)->s->ref_count);
                lock_release((*_r)->s->lock);
            }
        }
        (*_r)->s = *s;
        lock_get((*_r)->s->lock);
        (*_r)->s->ref_count++;
        lock_release((*_r)->s->lock);
    }

    run_ul_callbacks((*_r)->cbs, UL_IMPU_UPDATE, *_r, NULL);
    return 0;

out_of_memory:
    unlock_udomain(_d, public_identity);
    return -1;
}
