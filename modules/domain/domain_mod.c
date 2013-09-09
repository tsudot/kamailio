/*
 * $Id$
 *
 * Domain module
 *
 * Copyright (C) 2002-2012 Juha Heinanen
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
 * History:
 * -------
 * 2003-03-11: New module interface (janakj)
 * 2003-03-16: flags export parameter added (janakj)
 * 2003-04-05: default_uri #define used (jiri)
 * 2003-04-06: db connection closed in mod_init (janakj)
 * 2004-06-06: updated to the new DB api, cleanup: static dbf & handler,
 *             calls to domain_db_{bind,init,close,ver} (andrei)
 * 2006-01-22: added is_domain_local(variable) function (dan)
 *
 */


#include "domain_mod.h"
#include <stdio.h>
#include "../../mem/shm_mem.h"
#include "../../mem/mem.h"
#include "../../sr_module.h"
#include "../../pvar.h"
#include "../../forward.h"
#include "../../mod_fix.h"
#include "../../rpc.h"
#include "../../rpc_lookup.h"
#include "domain.h"
#include "mi.h"
#include "hash.h"
#include "api.h"
#include "../../locking.h"

/*
 * Module management function prototypes
 */
static int mod_init(void);
static void destroy(void);
static int child_init(int rank);

MODULE_VERSION

/*
 * Version of domain table required by the module,
 * increment this value if you change the table in
 * an backwards incompatible way
 */
#define DOMAIN_TABLE_VERSION 2
#define DOMAIN_ATTRS_TABLE_VERSION 1

#define DOMAIN_TABLE "domain"
#define DOMAIN_TABLE_LEN (sizeof(DOMAIN_TABLE) - 1)

#define DOMAIN_ATTRS_TABLE "domain_attrs"
#define DOMAIN_ATTRS_TABLE_LEN (sizeof(DOMAIN_ATTRS_TABLE) - 1)

#define DID_COL "did"
#define DID_COL_LEN (sizeof(DID_COL) - 1)

#define DOMAIN_COL "domain"
#define DOMAIN_COL_LEN (sizeof(DOMAIN_COL) - 1)

#define NAME_COL "name"
#define NAME_COL_LEN (sizeof(NAME_COL) - 1)

#define TYPE_COL "type"
#define TYPE_COL_LEN (sizeof(TYPE_COL) - 1)

#define VALUE_COL "value"
#define VALUE_COL_LEN (sizeof(VALUE_COL) - 1)

static int domain_init_rpc(void);

/*
 * Module parameter variables
 */
str db_url = {DEFAULT_RODB_URL, DEFAULT_RODB_URL_LEN};
str domain_table = {DOMAIN_TABLE, DOMAIN_TABLE_LEN}; /* Name of domain table */
str domain_attrs_table = {DOMAIN_ATTRS_TABLE, DOMAIN_ATTRS_TABLE_LEN};
str did_col = {DID_COL, DID_COL_LEN};       /* Name of domain id column */
str domain_col = {DOMAIN_COL, DOMAIN_COL_LEN};       /* Name of domain column */
str name_col = {NAME_COL, NAME_COL_LEN};   /* Name of attribute name column */
str type_col = {TYPE_COL, TYPE_COL_LEN};   /* Name of attribute type column */
str value_col = {VALUE_COL, VALUE_COL_LEN}; /* Name of attribute value column */
int domain_reg_myself = 0;

/*
 * Other module variables
 */
struct domain_list ***hash_table = 0;	/* Pointer to current hash table pointer */
struct domain_list **hash_table_1 = 0;	/* Pointer to hash table 1 */
struct domain_list **hash_table_2 = 0;	/* Pointer to hash table 2 */
gen_lock_t *reload_lock;

/*
 * Exported functions
 */
static cmd_export_t cmds[] = {
	{"is_from_local", (cmd_function)is_from_local, 0, 0, 0,
	 REQUEST_ROUTE|BRANCH_ROUTE|FAILURE_ROUTE|LOCAL_ROUTE},
	{"is_uri_host_local", (cmd_function)is_uri_host_local, 0, 0, 0,
	 REQUEST_ROUTE|BRANCH_ROUTE|FAILURE_ROUTE|LOCAL_ROUTE},
	{"is_domain_local", (cmd_function)w_is_domain_local, 1, fixup_pvar_null,
	 fixup_free_pvar_null,
	 REQUEST_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE|LOCAL_ROUTE},
	{"lookup_domain", (cmd_function)w_lookup_domain_no_prefix, 1,
	 fixup_pvar_null, fixup_free_pvar_null,
	 REQUEST_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE|LOCAL_ROUTE},
	{"lookup_domain", (cmd_function)w_lookup_domain, 2, fixup_pvar_str,
	 fixup_free_pvar_str,
	 REQUEST_ROUTE|FAILURE_ROUTE|BRANCH_ROUTE|LOCAL_ROUTE},
	{"bind_domain", (cmd_function)bind_domain, 1, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};


/*
 * Exported parameters
 */
static param_export_t params[] = {
	{"db_url",         STR_PARAM, &db_url.s      },
	{"domain_table",   STR_PARAM, &domain_table.s},
	{"domain_attrs_table",   STR_PARAM, &domain_attrs_table.s},
	{"did_col",        STR_PARAM, &did_col.s  },
	{"domain_col",     STR_PARAM, &domain_col.s  },
	{"name_col",       STR_PARAM, &name_col.s  },
	{"type_col",       STR_PARAM, &type_col.s  },
	{"value_col",      STR_PARAM, &value_col.s  },
	{"register_myself",INT_PARAM, &domain_reg_myself},
	{0, 0, 0}
};


/*
 * Exported MI functions
 */
static mi_export_t mi_cmds[] = {
    { MI_DOMAIN_RELOAD, mi_domain_reload, MI_NO_INPUT_FLAG, 0, 0 },
    { MI_DOMAIN_DUMP,   mi_domain_dump,   MI_NO_INPUT_FLAG, 0, 0 },
    { 0, 0, 0, 0, 0}
};


/*
 * Module interface
 */
struct module_exports exports = {
	"domain",
	DEFAULT_DLFLAGS, /* dlopen flags */
	cmds,      /* Exported functions */
	params,    /* Exported parameters */
	0,         /* exported statistics */
	mi_cmds,   /* exported MI functions */
	0,         /* exported pseudo-variables */
	0,         /* extra processes */
	mod_init,  /* module initialization function */
	0,         /* response function*/
	destroy,   /* destroy function */
	child_init /* per-child init function */
};


static int mod_init(void)
{
    LM_DBG("initializing\n");
	
    if (register_mi_mod(exports.name, mi_cmds) != 0) {
	LM_ERR("failed to register MI commands\n");
	return -1;
    }
    if (domain_init_rpc() != 0) {
	LM_ERR("failed to register RPC commands\n");
	return -1;
    }

    if (domain_reg_myself !=0 ) {
	if (register_check_self_func(domain_check_self) <0 ) {
	    LM_ERR("failed to register check self function\n");
	    return -1;
	}
    }

    db_url.len = strlen(db_url.s);
    domain_table.len = strlen(domain_table.s);
    domain_attrs_table.len = strlen(domain_attrs_table.s);
    did_col.len = strlen(did_col.s);
    domain_col.len = strlen(domain_col.s);
    name_col.len = strlen(name_col.s);
    type_col.len = strlen(type_col.s);
    value_col.len = strlen(value_col.s);

    /* Bind database */
    if (domain_db_bind(&db_url)) {
	LM_ERR("no database module found\n");
	return -1;
    }

    /* Check table versions */
    if (domain_db_init(&db_url) < 0) {
	LM_ERR("unable to open database connection\n");
	return -1;
    }
    if (domain_db_ver(&domain_table, DOMAIN_TABLE_VERSION) < 0) {
	LM_ERR("error during check of domain table version\n");
	domain_db_close();
	goto error;
    }
    if (domain_db_ver(&domain_attrs_table, DOMAIN_ATTRS_TABLE_VERSION) < 0) {
	LM_ERR("error during check of domain_attrs table version\n");
	domain_db_close();
	goto error;
    }
    domain_db_close();

    /* Initializing hash tables and hash table variable */
    hash_table = (struct domain_list ***)shm_malloc
	(sizeof(struct domain_list *));
    hash_table_1 = (struct domain_list **)shm_malloc
	(sizeof(struct domain_list *) * (DOM_HASH_SIZE + 1));
    hash_table_2 = (struct domain_list **)shm_malloc
	(sizeof(struct domain_list *) * (DOM_HASH_SIZE + 1));
    if ((hash_table == 0) || (hash_table_1 == 0) || (hash_table_2 == 0)) {
	LM_ERR("no memory for hash table\n");
	goto error;
    }
    memset(hash_table_1, 0, sizeof(struct domain_list *) *
	   DOM_HASH_SIZE + 1);
    memset(hash_table_2, 0, sizeof(struct domain_list *) *
	   DOM_HASH_SIZE + 1);
    *hash_table = hash_table_1;

    /* Allocate and initialize locks */
    reload_lock = lock_alloc();
    if (reload_lock == NULL) {
	LM_ERR("cannot allocate reload_lock\n");
	goto error;
    }
    if (lock_init(reload_lock) == NULL) {
	LM_ERR("cannot init reload_lock\n");
	goto error;
    }

    /* First reload */
    lock_get(reload_lock);
    if (reload_tables() == -1) {
	lock_release(reload_lock);
	LM_CRIT("domain reload failed\n");
	goto error;
    }
    lock_release(reload_lock);

    return 0;

error:
    destroy();
    return -1;
}


static int child_init(int rank)
{
    return 0;
}


static void destroy(void)
{
    /* Destroy is called from the main process only,
     * there is no need to close database here because
     * it is closed in mod_init already
     */
    if (hash_table) {
	shm_free(hash_table);
	hash_table = 0;
    }
    if (hash_table_1) {
	hash_table_free(hash_table_1);
	shm_free(hash_table_1);
	hash_table_1 = 0;
    }
    if (hash_table_2) {
	hash_table_free(hash_table_2);
	shm_free(hash_table_2);
	hash_table_2 = 0;
    }
}


static const char* domain_rpc_reload_doc[2] = {
    "Reload domain tables from database",
    0
};


/*
 * RPC command to reload domain table
 */
static void domain_rpc_reload(rpc_t* rpc, void* ctx)
{
    lock_get(reload_lock);
    if (reload_tables() < 0) {
	rpc->fault(ctx, 400, "Reload of domain tables failed");
    }
    lock_release(reload_lock);
}


static const char* domain_rpc_dump_doc[2] = {
    "Return the contents of domain and domain_attrs tables",
    0
};


/*
 * Fifo function to print domains from current hash table
 */
static void domain_rpc_dump(rpc_t* rpc, void* ctx)
{
    int i;
    struct domain_list *np;
    struct attr_list *ap;
    struct domain_list **ht;
    void* st;

    if(hash_table==0 || *hash_table==0) {
	rpc->fault(ctx, 404, "Server Domain Cache Empty");
	return;
    }
    ht = *hash_table;
    for (i = 0; i < DOM_HASH_SIZE; i++) {
	np = ht[i];
	while (np) {
	    if (rpc->add(ctx, "{", &st) < 0) return;
	    rpc->struct_add(st, "SS",
			    "domain", &np->domain,
			    "did", &np->did);
	    np = np->next;
	}
    }
    np = ht[DOM_HASH_SIZE];
    while (np) {
	if (rpc->add(ctx, "{", &st) < 0) return;
	rpc->struct_add(st, "S",
			"did", &np->did);
	ap = np->attrs;
	while (ap) {
	    rpc->struct_add(st, "S",
			    "attr", &ap->name);
	    ap = ap->next;
	}
	np = np->next;
    }

    return;
}


rpc_export_t domain_rpc_list[] = {
    {"domain.reload", domain_rpc_reload, domain_rpc_reload_doc, 0},
    {"domain.dump",   domain_rpc_dump,   domain_rpc_dump_doc,   0},
    {0, 0, 0, 0}
};

static int domain_init_rpc(void)
{
    if (rpc_register_array(domain_rpc_list) != 0) {
	LM_ERR("failed to register RPC commands\n");
	return -1;
    }
    return 0;
}
