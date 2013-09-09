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

#include <stdio.h>
#include "ul_mod.h"
#include "../../sr_module.h"
#include "../../dprint.h"
#include "../../rpc_lookup.h"
#include "../../timer.h"     /* register_timer */
#include "../../globals.h"   /* is_main */
#include "../../ut.h"        /* str_init */
#include "dlist.h"           /* register_udomain */
#include "udomain.h"         /* {insert,delete,get,release}_urecord */
#include "pcontact.h"         /* {insert,delete,get}_ucontact */
#include "ul_rpc.h"
#include "ul_callback.h"
#include "usrloc.h"

MODULE_VERSION

#define DEFAULT_DBG_FILE "/var/log/usrloc_debug"
static FILE *debug_file;

static int mod_init(void);                          /*!< Module initialization function */
static void destroy(void);                          /*!< Module destroy function */
static void timer(unsigned int ticks, void* param); /*!< Timer handler */
static int child_init(int rank);                    /*!< Per-child init function */

extern int bind_usrloc(usrloc_api_t* api);
extern int ul_locks_no;

/*
 * Module parameters and their default values
 */
str usrloc_debug_file = str_init(DEFAULT_DBG_FILE);
int timer_interval  = 60;				/*!< Timer interval in seconds */
int usrloc_debug 	= 0;
int ul_hash_size = 9;
int init_flag = 0;

/*! \brief
 * Exported functions
 */
static cmd_export_t cmds[] = {
	{"ul_bind_ims_usrloc_pcscf",        (cmd_function)bind_usrloc,        1, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};

/*! \brief
 * Exported parameters
 */
static param_export_t params[] = {
	{"hash_size",         INT_PARAM, &ul_hash_size    },
	{"timer_interval",    INT_PARAM, &timer_interval  },
	{"usrloc_debug_file", STR_PARAM, &usrloc_debug_file.s},
	{"enable_debug_file", INT_PARAM, &usrloc_debug},
	{0, 0, 0}
};

stat_export_t mod_stats[] = {
	{"registered_contacts" ,  STAT_IS_FUNC, (stat_var**)get_number_of_contacts  },
	{"registered_impus" ,  STAT_IS_FUNC, (stat_var**)get_number_of_impu  },
	{"expired_contacts" ,  STAT_IS_FUNC, (stat_var**)get_number_of_expired  },
	{0,0,0}
};

static mi_export_t mi_cmds[] = {
	{ 0, 0, 0, 0, 0}
};

struct module_exports exports = {
	"ims_usrloc_pcscf",
	DEFAULT_DLFLAGS, /*!< dlopen flags */
	cmds,       /*!< Exported functions */
	params,     /*!< Export parameters */
	mod_stats,  /*!< exported statistics */
	mi_cmds,    /*!< exported MI functions */
	0,          /*!< exported pseudo-variables */
	0,          /*!< extra processes */
	mod_init,   /*!< Module initialization function */
	0,          /*!< Response function */
	destroy,    /*!< Destroy function */
	child_init  /*!< Child initialization function */
};


/*! \brief
 * Module initialization function
 */
static int mod_init(void) {

	if (usrloc_debug){
		LM_INFO("Logging usrloc records to %.*s\n", usrloc_debug_file.len, usrloc_debug_file.s);
		debug_file = fopen(usrloc_debug_file.s, "a");
		fprintf(debug_file, "starting\n");
		fflush(debug_file);
	}

#ifdef STATISTICS
	/* register statistics */
	if (register_module_stats( exports.name, mod_stats)!=0 ) {
		LM_ERR("failed to register core statistics\n");
		return -1;
	}
#endif

	/* Compute the lengths of string parameters */
	usrloc_debug_file.len = strlen(usrloc_debug_file.s);

	if (ul_hash_size <= 1)
		ul_hash_size = 512;
	else
		ul_hash_size = 1 << ul_hash_size;
	ul_locks_no = ul_hash_size;

	if (ul_init_locks() != 0) {
		LM_ERR("locks array initialization failed\n");
		return -1;
	}

	/* Regsiter RPC */
	if (rpc_register_array(ul_rpc) != 0) {
		LM_ERR("failed to register RPC commands\n");
		return -1;
	}

	/* Register cache timer */
	LM_DBG("Registering cache timer");
	register_timer(timer, 0, timer_interval);

	/* init the callbacks list */
	if (init_ulcb_list() < 0) {
		LM_ERR("usrloc/callbacks initialization failed\n");
		return -1;
	}

	init_flag = 1;

	return 0;
}

static int child_init(int rank)
{
	return 0;
}

/*! \brief
 * Module destroy function
 */
static void destroy(void)
{
	free_all_udomains();
	ul_destroy_locks();

	/* free callbacks list */
	destroy_ulcb_list();
}


/*! \brief
 * Timer handler
 */
static void timer(unsigned int ticks, void* param) {
	LM_DBG("Syncing cache\n");
	if (usrloc_debug) {
		print_all_udomains(debug_file);
		fflush(debug_file);
	}

	if (synchronize_all_udomains() != 0) {
		LM_ERR("synchronizing cache failed\n");
	}
}

