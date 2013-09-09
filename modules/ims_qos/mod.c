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

#include "stats.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../sr_module.h"
#include "../../events.h"
#include "../../dprint.h"
#include "../../ut.h"
#include "../../lib/ims/ims_getters.h"
#include "../tm/tm_load.h"
#include "../../mod_fix.h"
#include "../../parser/parse_uri.h"
#include "../../parser/parse_content.h"
#include "../ims_usrloc_pcscf/usrloc.h"
#include "../../modules/dialog_ng/dlg_load.h"
#include "../../modules/dialog_ng/dlg_hash.h"
#include "../cdp/cdp_load.h"
#include "../cdp_avp/mod_export.h"
#include "../../cfg/cfg_struct.h"
#include "cdpeventprocessor.h"
#include "rx_authdata.h"
#include "rx_asr.h"
#include "rx_str.h"
#include "rx_aar.h"
#include "mod.h"

#include "../../lib/ims/useful_defs.h"


MODULE_VERSION

extern gen_lock_t* process_lock; /* lock on the process table */

str orig_session_key = {"originating", 11};
str term_session_key = {"terminating", 11};

int rx_auth_expiry = 7200;

int must_send_str = 1;

struct tm_binds tmb;
struct cdp_binds cdpb;
struct dlg_binds dlgb;
bind_usrloc_t bind_usrloc;
cdp_avp_bind_t *cdp_avp;
usrloc_api_t ul;

int cdp_event_latency = 1; /*flag: report slow processing of CDP callback events or not - default enabled */
int cdp_event_threshold = 500; /*time in ms above which we should report slow processing of CDP callback event - default 500ms*/
int cdp_event_latency_loglevel = 0; /*log-level to use to report slow processing of CDP callback event - default ERROR*/

/** module functions */
static int mod_init(void);
static int mod_child_init(int);
static void mod_destroy(void);

static int fixup_aar_register(void** param, int param_no);

static void free_dialog_data(void *data);

int * callback_singleton; /*< Callback singleton */

/* parameters storage */
char* rx_dest_realm_s = "ims.smilecoms.com";
str rx_dest_realm;
/* Only used if we want to force the Rx peer usually this is configured at a stack level and the first request uses realm routing */
char* rx_forced_peer_s = "";
str rx_forced_peer;

/* commands wrappers and fixups */
static int w_rx_aar(struct sip_msg *msg, char* direction, char *bar);
static int w_rx_aar_register(struct sip_msg *msg, char* str1, char *bar);

static cmd_export_t cmds[] = {
    { "Rx_AAR", (cmd_function) w_rx_aar, 1, 0, 0, REQUEST_ROUTE | ONREPLY_ROUTE},
    { "Rx_AAR_Register", (cmd_function) w_rx_aar_register, 1, fixup_aar_register, 0, REQUEST_ROUTE},
    { 0, 0, 0, 0, 0, 0}
};

static param_export_t params[] = {
    { "rx_dest_realm", STR_PARAM, &rx_dest_realm_s},
    { "rx_forced_peer", STR_PARAM, &rx_forced_peer_s},
    { "rx_auth_expiry", INT_PARAM, &rx_auth_expiry},
    { "cdp_event_latency", INT_PARAM, &cdp_event_latency}, /*flag: report slow processing of CDP callback events or not */
    { "cdp_event_threshold", INT_PARAM, &cdp_event_threshold}, /*time in ms above which we should report slow processing of CDP callback event*/
    { "cdp_event_latency_log", INT_PARAM, &cdp_event_latency_loglevel}, /*log-level to use to report slow processing of CDP callback event*/
    { 0, 0, 0}
};

stat_export_t mod_stats[] = {
	{"aar_avg_response_time" ,  STAT_IS_FUNC, 	(stat_var**)get_avg_aar_response_time	},
	{"aar_timeouts" ,  			0, 				(stat_var**)&stat_aar_timeouts  		},
	{0,0,0}
};

/** module exports */
struct module_exports exports = {"ims_qos", DEFAULT_DLFLAGS, /* dlopen flags */
    cmds, /* Exported functions */
    params, 0, /* exported statistics */
    0, /* exported MI functions */
    0, /* exported pseudo-variables */
    0, /* extra processes */
    mod_init, /* module initialization function */
    0, mod_destroy, mod_child_init /* per-child init function */};

int fix_parameters() {
    rx_dest_realm.s = rx_dest_realm_s;
    rx_dest_realm.len = strlen(rx_dest_realm_s);

    rx_forced_peer.s = rx_forced_peer_s;
    rx_forced_peer.len = strlen(rx_forced_peer_s);

    return RX_RETURN_TRUE;
}

/**
 * init module function
 */
static int mod_init(void) {

    /* fix the parameters */
    if (!fix_parameters())
        goto error;

#ifdef STATISTICS
	/* register statistics */
	if (register_module_stats( exports.name, mod_stats)!=0 ) {
		LM_ERR("failed to register core statistics\n");
		goto error;
	}

	if (!register_stats()){
		LM_ERR("Unable to register statistics\n");
		goto error;
	}
#endif

    callback_singleton = shm_malloc(sizeof (int));
    *callback_singleton = 0;

    /*register space for event processor*/
    register_procs(1);

    cdp_avp = 0;
    /* load the TM API */
    if (load_tm_api(&tmb) != 0) {
        LM_ERR("can't load TM API\n");
        goto error;
    }

    /* load the CDP API */
    if (load_cdp_api(&cdpb) != 0) {
        LM_ERR("can't load CDP API\n");
        goto error;
    }

    /* load the dialog API */
    if (load_dlg_api(&dlgb) != 0) {
        LM_ERR("can't load Dialog API\n");
        goto error;
    }

    cdp_avp = load_cdp_avp();
    if (!cdp_avp) {
        LM_ERR("can't load CDP_AVP API\n");
        goto error;
    }

    /* load the usrloc API */
    bind_usrloc = (bind_usrloc_t) find_export("ul_bind_ims_usrloc_pcscf", 1, 0);
    if (!bind_usrloc) {
        LM_ERR("can't bind usrloc_pcscf\n");
        return RX_RETURN_FALSE;
    }

    if (bind_usrloc(&ul) < 0) {
        LM_ERR("can't bind to usrloc pcscf\n");
        return RX_RETURN_FALSE;
    }
    LM_DBG("Successfully bound to PCSCF Usrloc module\n");

    LM_DBG("Diameter RX interface successfully bound to TM, Dialog, Usrloc and CDP modules\n");

    /*init cdb cb event list*/
    if (!init_cdp_cb_event_list()) {
        LM_ERR("unable to initialise cdp callback event list\n");
        return -1;
    }

    return 0;
error:
    LM_ERR("Failed to initialise ims_qos module\n");
    return RX_RETURN_FALSE;
}

/**
 * Initializes the module in child.
 */
static int mod_child_init(int rank) {
    LM_DBG("Initialization of module in child [%d] \n", rank);

    if (rank == PROC_MAIN) {
        int pid = fork_process(PROC_NOCHLDINIT, "Rx Event Processor", 1);
        if (pid < 0)
            return -1; //error
        if (pid == 0) {
            if (cfg_child_init())
                return -1; //error
            cdp_cb_event_process();
        }
    }
    /* don't do anything for main process and TCP manager process */
    if (rank == PROC_MAIN || rank == PROC_TCP_MAIN) {
        return 0;
    }

    lock_get(process_lock);
    if ((*callback_singleton) == 0) {
        *callback_singleton = 1;
        cdpb.AAAAddRequestHandler(callback_cdp_request, NULL);
    }
    lock_release(process_lock);

    return 0;
}

static void mod_destroy(void) {
}

/*callback of CDP session*/
void callback_for_cdp_session(int event, void *session) {
    rx_authsessiondata_t* p_session_data = 0;
    AAASession *x = session;

    str* rx_session_id = (str*) & x->id;
    p_session_data = (rx_authsessiondata_t*) x->u.auth.generic_data;

    if (!rx_session_id || rx_session_id->len <= 0 || !rx_session_id->s) {
        LM_ERR("Invalid Rx session id");
        return;
    }

    if (!p_session_data) {
        LM_ERR("Invalid associated session data\n");
        return;
    }

    //only put the events we care about on the event stack
    if (event == AUTH_EV_SESSION_TIMEOUT ||
            event == AUTH_EV_SESSION_GRACE_TIMEOUT ||
            event == AUTH_EV_SESSION_LIFETIME_TIMEOUT ||
            event == AUTH_EV_SERVICE_TERMINATED) {

        LOG(L_DBG, "callback_for_cdp session(): called with event %d and session id [%.*s]\n", event, rx_session_id->len, rx_session_id->s);

        //create new event to process async
        cdp_cb_event_t *new_event = new_cdp_cb_event(event, rx_session_id, p_session_data);
        if (!new_event) {
            LM_ERR("Unable to create event for cdp callback\n");
            return;
        }
        //push the new event onto the stack (FIFO)
        push_cdp_cb_event(new_event);
    } else {
        LM_DBG("Ignoring event [%d] from CDP session\n", event);
    }
}

/**
 * Handler for incoming Diameter requests.
 * @param request - the received request
 * @param param - generic pointer
 * @returns the answer to this request
 */
AAAMessage* callback_cdp_request(AAAMessage *request, void *param) {
    if (is_req(request)) {

        switch (request->applicationId) {
            case IMS_Rx:
            case IMS_Gq:
                switch (request->commandCode) {
                    case IMS_RAR:
                        LM_INFO("Rx request handler():- Received an IMS_RAR \n");
                        /* TODO: Add support for Re-Auth Requests */
                        return 0;
                        break;
                    case IMS_ASR:
                        LM_INFO("Rx request handler(): - Received an IMS_ASR \n");
                        return rx_process_asr(request);
                        break;
                    default:
                        LM_ERR("Rx request handler(): - Received unknown request for Rx/Gq command %d, flags %#1x endtoend %u hopbyhop %u\n", request->commandCode, request->flags, request->endtoendId, request->hopbyhopId);
                        return 0;
                        break;
                }
                break;
            default:
                LM_ERR("Rx request handler(): - Received unknown request for app %d command %d\n", request->applicationId, request->commandCode);
                return 0;
                break;
        }
    }
    return 0;
}

static void free_dialog_data(void *data) {
    str *rx_session_id = (str*) data;
    if (rx_session_id) {
        if (rx_session_id->s) {
            shm_free(rx_session_id->s);
            rx_session_id->s = 0;
        }
        shm_free(rx_session_id);
        rx_session_id = 0;
    }

}

void callback_dialog_terminated(struct dlg_cell* dlg, int type, struct dlg_cb_params * params) {
    LM_DBG("Dialog has ended - we need to terminate Rx bearer session\n");

    str *rx_session_id;
    rx_session_id = (str*) * params->param;

    if (!rx_session_id) {
        LM_ERR("Invalid Rx session id");
        return;
    }

    LM_DBG("Received notification of termination of dialog with Rx session ID: [%.*s]\n",
            rx_session_id->len, rx_session_id->s);

    LM_DBG("Retrieving Rx auth data for this session id");

    LM_DBG("Sending STR\n");
    rx_send_str(rx_session_id);

    return;
}

void callback_pcscf_contact_cb(struct pcontact *c, int type, void *param) {
    LM_DBG("----------------------!\n");
    LM_DBG("PCSCF Contact Callback!\n");
    LM_DBG("Contact AOR: [%.*s]\n", c->aor.len, c->aor.s);
    LM_DBG("Callback type [%d]\n", type);


    if (type == PCSCF_CONTACT_EXPIRE || type == PCSCF_CONTACT_DELETE) {
        //we dont need to send STR if no QoS was ever succesfully registered!
        if (must_send_str && (c->reg_state != PCONTACT_REG_PENDING) && (c->reg_state != PCONTACT_REG_PENDING_AAR)) {
            LM_DBG("Received notification of contact (in state [%d] deleted for signalling bearer with  with Rx session ID: [%.*s]\n",
                    c->reg_state, c->rx_session_id.len, c->rx_session_id.s);
            LM_DBG("Sending STR\n");
            rx_send_str(&c->rx_session_id);
        }
    }
}

/* Wrapper to send AAR from config file - this only allows for AAR for calls - not register, which uses r_rx_aar_register
 * return: 1 - success, <=0 failure. 2 - message not a AAR generating message (ie proceed without PCC if you wish)
 */
static int w_rx_aar(struct sip_msg *msg, char* direction, char* bar) {
    struct cell *t;
    AAAMessage* resp;
    AAASession* auth_session;
    rx_authsessiondata_t* rx_authdata_p = 0;
    unsigned int result = AAA_SUCCESS;
    str *rx_session_id;
    str callid = {0, 0};
    str ftag = {0, 0};
    str ttag = {0, 0};

    //We don't ever do AAR on request for calling scenario...
    if (msg->first_line.type != SIP_REPLY) {
        LM_DBG("Can't do AAR for call session in request\n");
        goto error;
    }

    //is it appropriate to send AAR at this stage?
    t = tmb.t_gett();
    if (!t) {
        LM_WARN("Cannot get transaction for AAR based on SIP Request\n");
        goto aarna;
    }

    //we dont apply QoS if its not a reply to an INVITE! or UPDATE or PRACK!
    if ((t->method.len == 5 && memcmp(t->method.s, "PRACK", 5) == 0)
            || (t->method.len == 6 && (memcmp(t->method.s, "INVITE", 6) == 0
            || memcmp(t->method.s, "UPDATE", 6) == 0))) {
        if (cscf_get_content_length(msg) == 0
                || cscf_get_content_length(t->uas.request) == 0) {
            goto aarna; //AAR na if we dont have offer/answer pair
        }
    } else {
        goto aarna;
    }

    /* get callid, from and to tags to be able to identify dialog */
    callid = cscf_get_call_id(msg, 0);
    if (callid.len <= 0 || !callid.s) {
        LM_ERR("unable to get callid\n");
        goto error;
    }
    if (!cscf_get_from_tag(msg, &ftag)) {
        LM_ERR("Unable to get ftag\n");
        goto error;
    }
    if (!cscf_get_to_tag(msg, &ttag)) {
        LM_ERR("Unable to get ttag\n");
        goto error;
    }

    //check to see that this is not a result of a retransmission in reply route only
    if (msg->cseq == NULL
            && ((parse_headers(msg, HDR_CSEQ_F, 0) == -1) || (msg->cseq == NULL))) {
        LM_ERR("No Cseq header found - aborting\n");
        goto error;
    }

    //Check that we dont already have an auth session for this specific dialog
    //if not we create a new one and attach it to the dialog (via session ID).
    enum dialog_direction dlg_direction = get_dialog_direction(direction);
    if (dlg_direction == DLG_MOBILE_ORIGINATING) {
        rx_session_id = dlgb.get_dlg_var(&callid, &ftag, &ttag,
                &orig_session_key);
    } else {
        rx_session_id = dlgb.get_dlg_var(&callid, &ftag, &ttag,
                &term_session_key);
    }

    if (!rx_session_id || rx_session_id->len <= 0 || !rx_session_id->s) {
        LM_DBG("New AAR session for this dialog in mode %s\n", direction);
        //create new diameter auth session
        int ret = create_new_callsessiondata(&callid, &ftag, &ttag, &rx_authdata_p);
        if (!ret) {
            LM_DBG("Unable to create new media session data parcel\n");
            goto error;
        }
        auth_session = cdpb.AAACreateClientAuthSession(1, callback_for_cdp_session, rx_authdata_p); //returns with a lock
        if (!auth_session) {
            LM_ERR("Rx: unable to create new Rx Media Session\n");
            if (auth_session) cdpb.AAASessionsUnlock(auth_session->hash);
            if (rx_authdata_p) {
                shm_free(rx_authdata_p);
                rx_authdata_p = 0;
            }
            goto error;
        }

        //attach new cdp auth session to dlg for this direction
        if (dlg_direction == DLG_MOBILE_ORIGINATING) {
            dlgb.set_dlg_var(&callid, &ftag, &ttag,
                    &orig_session_key, &auth_session->id);
        } else {
            dlgb.set_dlg_var(&callid, &ftag, &ttag,
                    &term_session_key, &auth_session->id);
        }
        LM_DBG("Attached CDP auth session [%.*s] for Rx to dialog in %s mode\n", auth_session->id.len, auth_session->id.s, direction);
    } else {
        LM_DBG("Update AAR session for this dialog in mode %s\n", direction);
        //TODO - what to do on updates - reinvites, etc
        goto aarna; //TODO: for now we ignore
    }

    resp = rx_send_aar(t->uas.request, msg, auth_session, &callid, &ftag, &ttag,
            direction, &rx_authdata_p);

    if (!resp) {
        LM_ERR("No response received for AAR request\n");
        goto error;
    }

    if (!rx_authdata_p) {
        LM_ERR("Rx: mod.c: error creating new rx_auth_data\n");
        goto error;
    }
    //
    //    /* Process the response to AAR, retrieving result code and associated Rx session ID */
    if (rx_process_aaa(resp, &result) < 0) {
        LM_DBG("Failed to process AAA from PCRF\n");
        cdpb.AAAFreeMessage(&resp);
        goto error;
    }
    cdpb.AAAFreeMessage(&resp);


    if (result >= 2000 && result < 3000) {
        LM_DBG("Success, received code: [%i] from PCRF for AAR request\n", result);

        str * passed_rx_session_id = shm_malloc(sizeof (struct _str));

        passed_rx_session_id->s = 0;
        passed_rx_session_id->len = 0;
        STR_SHM_DUP(*passed_rx_session_id, auth_session->id, "cb_passed_rx_session_id");

        LM_DBG("passed rx session id %.*s", passed_rx_session_id->len, passed_rx_session_id->s);

        dlgb.register_dlgcb_nodlg(&callid, &ftag, &ttag, DLGCB_TERMINATED | DLGCB_DESTROY | DLGCB_EXPIRED, callback_dialog_terminated, (void*) (passed_rx_session_id), free_dialog_data);

        return RX_RETURN_TRUE;
    } else {
        LM_DBG("Received negative reply from PCRF for AAR Request\n");
        //we don't free rx_authdata_p here - it is free-ed when the CDP session expires
        goto error; // if its not a success then that means i want to reject this call!
    }
out_of_memory:
    error :
            LM_ERR("Error trying to send AAR (calling)\n");
    return RX_RETURN_FALSE;
aarna:
    LM_DBG("Policy and Charging Control non-applicable\n");
    return RX_RETURN_AAR_NA;
}

/* Wrapper to send AAR from config file - only used for registration */
static int w_rx_aar_register(struct sip_msg *msg, char* str1, char* bar) {

    int ret = CSCF_RETURN_ERROR;
    struct pcontact_info ci;
    struct cell *t;
    contact_t* c;
    struct hdr_field* h;
    pcontact_t* pcontact;
    contact_body_t* cb = 0;
    AAASession* auth;
    rx_authsessiondata_t* rx_regsession_data_p;
    cfg_action_t* cfg_action = 0;
    char* p;
    int aar_sent = 0;
    saved_transaction_local_t* local_data = 0;		//data to be shared across all async calls
    saved_transaction_t* saved_t_data = 0;			//data specific to each contact's AAR async call
    aar_param_t* ap = (aar_param_t*) str1;
    udomain_t* domain_t = ap->domain;
    cfg_action = ap->paction->next;
    int is_rereg = 0;								//is this a reg/re-reg

    LM_DBG("Rx AAR Register called\n");

    //create the default return code AVP
    create_return_code(ret);

    memset(&ci, 0, sizeof (struct pcontact_info));

    /** If this is a response then let's check the status before we try and do an AAR.
     * We will only do AAR for register on success response and of course if message is register
     */
    if (msg->first_line.type == SIP_REPLY) {
        //check this is a response to a register
        /* Get the SIP request from this transaction */
        t = tmb.t_gett();
        if (!t) {
            LM_ERR("Cannot get transaction for AAR based on SIP Request\n");
            goto error;
        }
        if ((strncmp(t->method.s, "REGISTER", 8) != 0)) {
            LM_ERR("Method is not a response to a REGISTER\n");
            goto error;
        }
        if (msg->first_line.u.reply.statuscode < 200
                || msg->first_line.u.reply.statuscode >= 300) {
            LM_DBG("Message is not a 2xx OK response to a REGISTER\n");
            goto error;
        }
        tmb.t_release(msg);
    } else { //SIP Request
        /* in case of request make sure it is a REGISTER */
        if (msg->first_line.u.request.method_value != METHOD_REGISTER) {
            LM_DBG("This is not a register request\n");
            goto error;
        }

        if ((cscf_get_max_expires(msg, 0) == 0)) {
            //if ((cscf_get_expires(msg) == 0)) {
            LM_DBG("This is a de registration\n");
            LM_DBG("We ignore it as these are dealt with by usrloc callbacks \n");
            create_return_code(RX_RETURN_TRUE);
            return RX_RETURN_TRUE;
        }
    }

    //before we continue, make sure we have a transaction to work with (viz. cdp async)
	t = tmb.t_gett();
	if (t == NULL || t == T_UNDEFINED) {
		if (tmb.t_newtran(msg) < 0) {
			LM_ERR("cannot create the transaction for UAR async\n");
			return CSCF_RETURN_ERROR;
		}
		t = tmb.t_gett();
		if (t == NULL || t == T_UNDEFINED) {
			LM_ERR("cannot lookup the transaction\n");
			return CSCF_RETURN_ERROR;
		}
	}

	saved_t_data = (saved_transaction_t*)shm_malloc(sizeof(saved_transaction_t));
	if (!saved_t_data){
		LM_ERR("Unable to allocate memory for transaction data, trying to send AAR\n");
		return CSCF_RETURN_ERROR;
	}
	memset(saved_t_data,0,sizeof(saved_transaction_t));
	saved_t_data->act = cfg_action;
	saved_t_data->domain = domain_t;
	saved_t_data->lock = lock_alloc();
	if (saved_t_data->lock == NULL) {
		LM_ERR("unable to allocate init lock for saved_t_transaction reply counter\n");
		return CSCF_RETURN_ERROR;
	}
	if (lock_init(saved_t_data->lock) == NULL) {
		LM_ERR("unable to init lock for saved_t_transaction reply counter\n");
		return CSCF_RETURN_ERROR;
	}

	LM_DBG("Suspending SIP TM transaction\n");
	if (tmb.t_suspend(msg, &saved_t_data->tindex, &saved_t_data->tlabel) < 0) {
		LM_ERR("failed to suspend the TM processing\n");
		free_saved_transaction_global_data(saved_t_data);
		return CSCF_RETURN_ERROR;
	}

	LM_DBG("Successfully suspended transaction\n");

	//now get the contacts in the REGISTER and do AAR for each one.
    cb = cscf_parse_contacts(msg);
    if (!cb || (!cb->contacts && !cb->star)) {
        LM_DBG("No contact headers in Register message\n");
        goto error;
    }

    lock_get(saved_t_data->lock);		//we lock here to make sure we send all requests before processing replies asynchronously
    for (h = msg->contact; h; h = h->next) {
        if (h->type == HDR_CONTACT_T && h->parsed) {
            for (c = ((contact_body_t*) h->parsed)->contacts; c; c = c->next) {
                ul.lock_udomain(domain_t, &c->uri);
                if (ul.get_pcontact(domain_t, &c->uri, &pcontact) != 0) {
                    LM_DBG("This contact does not exist in PCSCF usrloc - error in cfg file\n");
                    ul.unlock_udomain(domain_t, &c->uri);
                    lock_release(saved_t_data->lock);
                    goto error;
                } else if (pcontact->reg_state == PCONTACT_REG_PENDING
                        || pcontact->reg_state == PCONTACT_REGISTERED) { //NEW reg request
                    LM_DBG("Contact [%.*s] exists and is in state PCONTACT_REG_PENDING or PCONTACT_REGISTERED\n"
                    		, pcontact->aor.len, pcontact->aor.s);

                    //get IP address from contact
                    struct sip_uri puri;
                    if (parse_uri(c->uri.s, c->uri.len, &puri) < 0) {
                        LM_ERR("failed to parse Contact\n");
                        ul.unlock_udomain(domain_t, &c->uri);
                        lock_release(saved_t_data->lock);
                        goto error;

                    }
                    LM_DBG("Parsed URI of from host is [%.*s]\n", puri.host.len, puri.host.s);
                    uint16_t ip_version = AF_INET; //TODO IPv6!!!?

                    //check for existing Rx session
                    if (pcontact->rx_session_id.len > 0
                            && pcontact->rx_session_id.s
                            && (auth = cdpb.AAAGetAuthSession(pcontact->rx_session_id))) {
                        LM_DBG("Rx session already exists for this user\n");
                        if (memcmp(pcontact->rx_session_id.s, auth->id.s, auth->id.len) != 0) {
                            LM_ERR("Rx session mismatch when URI is [%.*s].......Aborting\n", puri.host.len, puri.host.s);
                            if (auth) cdpb.AAASessionsUnlock(auth->hash);
                            lock_release(saved_t_data->lock);
                            goto error;
                        }
                        //re-registration - update auth lifetime
                        auth->u.auth.lifetime = time(NULL) + rx_auth_expiry;
                        is_rereg = 1;
                    } else {
                        LM_DBG("Creating new Rx session for contact <%.*s>\n", pcontact->aor.len, pcontact->aor.s);
                        int ret = create_new_regsessiondata(domain_t->name, &pcontact->aor, &rx_regsession_data_p);
                        if (!ret) {
                            LM_ERR("Unable to create regsession data parcel when URI is [%.*s]...Aborting\n", puri.host.len, puri.host.s);
                            ul.unlock_udomain(domain_t, &c->uri);
                            if (rx_regsession_data_p) {
                                shm_free(rx_regsession_data_p);
                                rx_regsession_data_p = 0;
                            }
                            lock_release(saved_t_data->lock);
                            goto error;
                        }
                        auth = cdpb.AAACreateClientAuthSession(1, callback_for_cdp_session, rx_regsession_data_p); //returns with a lock
                        if (!auth) {
                            LM_ERR("Rx: unable to create new Rx Reg Session when URI is [%.*s]\n", puri.host.len, puri.host.s);
                            if (rx_regsession_data_p) {
                                shm_free(rx_regsession_data_p);
                                rx_regsession_data_p = 0;
                            }
                            ul.unlock_udomain(domain_t, &c->uri);
                            if (auth) cdpb.AAASessionsUnlock(auth->hash);
                            if (rx_regsession_data_p) {
                                shm_free(rx_regsession_data_p);
                                rx_regsession_data_p = 0;
                            }
                            lock_release(saved_t_data->lock);
                            goto error;
                        }
                    }

                    //we are ready to send the AAR async. lets save the local data data
                    int local_data_len = sizeof(saved_transaction_local_t) + c->uri.len + auth->id.len;
                    local_data = shm_malloc(local_data_len);
                    if (!local_data) {
                    	LM_ERR("unable to alloc memory for local data, trying to send AAR Register\n");
                    	lock_release(saved_t_data->lock);
                    	goto error;
                    }
                    memset(local_data, 0, local_data_len);

                    local_data->is_rereg = is_rereg;
                    local_data->global_data = saved_t_data;
                    p = (char*) (local_data + 1);

                    local_data->contact.s = p;
                    local_data->contact.len = c->uri.len;
                    memcpy(p, c->uri.s, c->uri.len);
                    p+=c->uri.len;

                    local_data->auth_session_id.s = p;
                    local_data->auth_session_id.len = auth->id.len;
                    memcpy(p, auth->id.s, auth->id.len);
                    p+=auth->id.len;

                    if (p!=( ((char*)local_data) + local_data_len) ) {
                    	LM_CRIT("buffer overflow\n");
                    	free_saved_transaction_data(local_data);
                    	goto error;
                    }

                    LM_DBG("Calling send aar register");
                    ret = rx_send_aar_register(msg, auth, &puri.host, &ip_version, &c->uri, local_data); //returns a locked rx auth object
                    ul.unlock_udomain(domain_t, &c->uri);

                    if (!ret) {
                    	LM_ERR("Failed to send AAR\n");
                    	lock_release(saved_t_data->lock);
                    	free_saved_transaction_data(local_data);	//free the local data becuase the CDP async request was not successful (we must free here)
                    	goto error;
                    } else {
                    	aar_sent = 1;
                    	//before we send - bump up the reply counter
			saved_t_data->answers_not_received++;		//we dont need to lock as we already hold the lock above
                    }
                } else {
                    //contact exists - this is a re-registration, for now we just ignore this
                    LM_DBG("This contact exists and is not in state REGISTER PENDING - we assume re (or de) registration and ignore\n");
                    ul.unlock_udomain(domain_t, &c->uri);
                    //now we loop for any other contacts.
                }
            }
        } else {
        	if (h->type == HDR_CONTACT_T) { //means we couldnt parse the contact - this is an error
        		LM_ERR("Failed to parse contact header\n");
        		lock_release(saved_t_data->lock);
        		goto error;
        	}
        }
    }
    //all requests sent at this point - we can unlock the reply lock
    lock_release(saved_t_data->lock);

    /*if we get here, we have either:
     * 1. Successfully sent AAR's for ALL contacts, or
     * 2. haven't needed to send ANY AAR's for ANY contacts
     */
    if (aar_sent) {
    	LM_DBG("Successful async send of AAR\n");
    	return RX_RETURN_BREAK; //on success we break - because rest of cfg file will be executed by async process
    } else {
    	create_return_code(RX_RETURN_TRUE);
    	free_saved_transaction_global_data(saved_t_data);	//no aar sent so we must free the global data
    	return RX_RETURN_TRUE;
    }
error:
    LM_ERR("Error trying to send AAR\n");
    if (!aar_sent)
    	if (saved_t_data)
    		free_saved_transaction_global_data(saved_t_data); 	//only free global data if no AARs were sent. if one was sent we have to rely on the callback (CDP) to free
    															//otherwise the callback will segfault
    return RX_RETURN_FALSE;
}

static int fixup_aar_register(void** param, int param_no)
{
	udomain_t* d;
	aar_param_t *ap;

	if(param_no!=1)
		return 0;
	ap = (aar_param_t*)pkg_malloc(sizeof(aar_param_t));
	if(ap==NULL)
	{
		LM_ERR("no more pkg\n");
		return -1;
	}
	memset(ap, 0, sizeof(aar_param_t));
	ap->paction = get_action_from_param(param, param_no);

	if (ul.register_udomain((char*) *param, &d) < 0) {
		LM_ERR("failed to register domain\n");
		return E_UNSPEC;
	}
	ap->domain = d;

	*param = (void*)ap;
	return 0;
}

/*create a return code to be passed back into config file*/
int create_return_code(int result) {
    int rc;
    int_str avp_val, avp_name;
    avp_name.s.s = "aar_return_code";
    avp_name.s.len = 15;

    LM_DBG("Creating return code of [%d] for aar_return_code\n", result);
    //build avp spec for uaa_return_code
    avp_val.n = result;

    rc = add_avp(AVP_NAME_STR, avp_name, avp_val);

    if (rc < 0)
        LM_ERR("couldn't create [aar_return_code] AVP\n");
    else
        LM_DBG("created AVP successfully : [%.*s]\n", avp_name.s.len, avp_name.s.s);

    return rc;
}
