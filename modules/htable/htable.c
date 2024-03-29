/**
 * $Id$
 *
 * Copyright (C) 2008 Elena-Ramona Modroiu (asipto.com)
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
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <fcntl.h>

#include "../../sr_module.h"
#include "../../timer.h"
#include "../../route.h"
#include "../../dprint.h"
#include "../../hashes.h"
#include "../../ut.h"
#include "../../rpc.h"
#include "../../rpc_lookup.h"
#include "../../lib/kmi/mi.h"
#include "../../lib/kcore/faked_msg.h"

#include "../../pvar.h"
#include "ht_api.h"
#include "ht_db.h"
#include "ht_var.h"
#include "api.h"


MODULE_VERSION

int  ht_timer_interval = 20;
int  ht_db_expires_flag = 0;

static int htable_init_rpc(void);

/** module functions */
static int ht_print(struct sip_msg*, char*, char*);
static int mod_init(void);
static int child_init(int rank);
static void destroy(void);

static int fixup_ht_key(void** param, int param_no);
static int ht_rm_name_re(struct sip_msg* msg, char* key, char* foo);
static int ht_rm_value_re(struct sip_msg* msg, char* key, char* foo);
static int ht_slot_lock(struct sip_msg* msg, char* key, char* foo);
static int ht_slot_unlock(struct sip_msg* msg, char* key, char* foo);

int ht_param(modparam_t type, void* val);

static struct mi_root* ht_mi_reload(struct mi_root* cmd_tree, void* param);
static struct mi_root* ht_mi_dump(struct mi_root* cmd_tree, void* param);
static struct mi_root* ht_mi_delete(struct mi_root* cmd_tree, void* param);

static pv_export_t mod_pvs[] = {
	{ {"sht", sizeof("sht")-1}, PVT_OTHER, pv_get_ht_cell, pv_set_ht_cell,
		pv_parse_ht_name, 0, 0, 0 },
	{ {"shtex", sizeof("shtex")-1}, PVT_OTHER, pv_get_ht_cell_expire,
		pv_set_ht_cell_expire,
		pv_parse_ht_name, 0, 0, 0 },
	{ {"shtcn", sizeof("shtcn")-1}, PVT_OTHER, pv_get_ht_cn, 0,
		pv_parse_ht_name, 0, 0, 0 },
	{ {"shtcv", sizeof("shtcv")-1}, PVT_OTHER, pv_get_ht_cv, 0,
		pv_parse_ht_name, 0, 0, 0 },
	{ {"shtinc", sizeof("shtinc")-1}, PVT_OTHER, pv_get_ht_inc, 0,
		pv_parse_ht_name, 0, 0, 0 },
	{ {"shtdec", sizeof("shtdec")-1}, PVT_OTHER, pv_get_ht_dec, 0,
		pv_parse_ht_name, 0, 0, 0 },
	{ {0, 0}, 0, 0, 0, 0, 0, 0, 0 }
};

static mi_export_t mi_cmds[] = {
	{ "sht_reload",     ht_mi_reload,  0,  0,  0},
	{ "sht_dump",       ht_mi_dump,    0,  0,  0},
	{ "sht_delete",     ht_mi_delete,  0,  0,  0},
	{ 0, 0, 0, 0, 0}
};


static cmd_export_t cmds[]={
	{"sht_print",       (cmd_function)ht_print,        0, 0, 0,
		ANY_ROUTE},
	{"sht_rm_name_re",  (cmd_function)ht_rm_name_re,   1, fixup_ht_key, 0,
		ANY_ROUTE},
	{"sht_rm_value_re", (cmd_function)ht_rm_value_re,  1, fixup_ht_key, 0,
		ANY_ROUTE},
	{"sht_lock",        (cmd_function)ht_slot_lock,    1, fixup_ht_key, 0,
		ANY_ROUTE},
	{"sht_unlock",      (cmd_function)ht_slot_unlock,  1, fixup_ht_key, 0,
		ANY_ROUTE},
	{"bind_htable",     (cmd_function)bind_htable,     0, 0, 0,
		ANY_ROUTE},
	{0,0,0,0,0,0}
};

static param_export_t params[]={
	{"htable",             STR_PARAM|USE_FUNC_PARAM, (void*)ht_param},
	{"db_url",             STR_PARAM, &ht_db_url.s},
	{"key_name_column",    STR_PARAM, &ht_db_name_column.s},
	{"key_type_column",    STR_PARAM, &ht_db_ktype_column.s},
	{"value_type_column",  STR_PARAM, &ht_db_vtype_column.s},
	{"key_value_column",   STR_PARAM, &ht_db_value_column.s},
	{"expires_column",     STR_PARAM, &ht_db_expires_column.s},
	{"array_size_suffix",  STR_PARAM, &ht_array_size_suffix.s},
	{"fetch_rows",         INT_PARAM, &ht_fetch_rows},
	{"timer_interval",     INT_PARAM, &ht_timer_interval},
	{"db_expires",         INT_PARAM, &ht_db_expires_flag},
	{0,0,0}
};


/** module exports */
struct module_exports exports= {
	"htable",
	DEFAULT_DLFLAGS, /* dlopen flags */
	cmds,
	params,
	0,          /* exported statistics */
	mi_cmds,    /* exported MI functions */
	mod_pvs,    /* exported pseudo-variables */
	0,          /* extra processes */
	mod_init,   /* module initialization function */
	0,
	(destroy_function) destroy,
	child_init  /* per-child init function */
};

/**
 * init module function
 */
static int mod_init(void)
{
	if(register_mi_mod(exports.name, mi_cmds)!=0)
	{
		LM_ERR("failed to register MI commands\n");
		return -1;
	}
	if(htable_init_rpc()!=0)
	{
		LM_ERR("failed to register RPC commands\n");
		return -1;
	}

	if(ht_init_tables()!=0)
		return -1;
	ht_db_init_params();

	if(ht_db_url.len>0)
	{
		if(ht_db_init_con()!=0)
			return -1;
		if(ht_db_open_con()!=0)
			return -1;
		if(ht_db_load_tables()!=0)
		{
			ht_db_close_con();
			return -1;
		}
		ht_db_close_con();
	}
	if(ht_has_autoexpire())
	{
		LM_DBG("starting auto-expire timer\n");
		if(ht_timer_interval<=0)
			ht_timer_interval = 20;
		if(register_timer(ht_timer, 0, ht_timer_interval)<0)
		{
			LM_ERR("failed to register timer function\n");
			return -1;
		}
	}
	return 0;
}


static int child_init(int rank)
{
	struct sip_msg *fmsg;
	struct run_act_ctx ctx;
	int rtb, rt;

	LM_DBG("rank is (%d)\n", rank);
	if (rank!=PROC_INIT)
		return 0;
	
	rt = route_get(&event_rt, "htable:mod-init");
	if(rt>=0 && event_rt.rlist[rt]!=NULL) {
		LM_DBG("executing event_route[htable:mod-init] (%d)\n", rt);
		if(faked_msg_init()<0)
			return -1;
		fmsg = faked_msg_next();
		rtb = get_route_type();
		set_route_type(REQUEST_ROUTE);
		init_run_actions_ctx(&ctx);
		run_top_route(event_rt.rlist[rt], fmsg, &ctx);
		if(ctx.run_flags&DROP_R_F)
		{
			LM_ERR("exit due to 'drop' in event route\n");
			return -1;
		}
		set_route_type(rtb);
	}

	return 0;
}

/**
 * destroy function
 */
static void destroy(void)
{
	/* sync back to db */
	if(ht_db_url.len>0)
	{
		if(ht_db_init_con()==0)
		{
			if(ht_db_open_con()==0)
			{
				ht_db_sync_tables();
				ht_db_close_con();
			}
		}
	}
	ht_destroy();
}

/**
 * print hash table content
 */
static int ht_print(struct sip_msg *msg, char *s1, char *s2)
{
	ht_dbg();
	return 1;
}

static int fixup_ht_key(void** param, int param_no)
{
	pv_spec_t *sp;
	str s;

	sp = (pv_spec_t*)pkg_malloc(sizeof(pv_spec_t));
	if(param_no != 1)
	{
		LM_ERR("invalid parameter number %d\n", param_no);
		return -1;
	}
	if (sp == 0)
	{
		LM_ERR("no pkg memory left\n");
		return -1;
	}
	memset(sp, 0, sizeof(pv_spec_t));
	s.s = (char*)*param; s.len = strlen(s.s);
	if(pv_parse_ht_name(sp, &s)<0)
	{
		pkg_free(sp);
		LM_ERR("invalid parameter %d\n", param_no);
		return -1;
	}
	*param = (void*)sp;
	return 0;
}

static int ht_rm_name_re(struct sip_msg* msg, char* key, char* foo)
{
	ht_pv_t *hpv;
	str sre;
	pv_spec_t *sp;
	sp = (pv_spec_t*)key;

	hpv = (ht_pv_t*)sp->pvp.pvn.u.dname;

	if(hpv->ht==NULL)
	{
		hpv->ht = ht_get_table(&hpv->htname);
		if(hpv->ht==NULL)
			return 1;
	}
	if(pv_printf_s(msg, hpv->pve, &sre)!=0)
	{
		LM_ERR("cannot get $ht expression\n");
		return -1;
	}
	if(ht_rm_cell_re(&sre, hpv->ht, 0)<0)
		return -1;
	return 1;
}

static int ht_rm_value_re(struct sip_msg* msg, char* key, char* foo)
{
	ht_pv_t *hpv;
	str sre;
	pv_spec_t *sp;
	sp = (pv_spec_t*)key;

	hpv = (ht_pv_t*)sp->pvp.pvn.u.dname;

	if(hpv->ht==NULL)
	{
		hpv->ht = ht_get_table(&hpv->htname);
		if(hpv->ht==NULL)
			return 1;
	}
	if(pv_printf_s(msg, hpv->pve, &sre)!=0)
	{
		LM_ERR("cannot get $ht expression\n");
		return -1;
	}

	if(ht_rm_cell_re(&sre, hpv->ht, 1)<0)
		return -1;
	return 1;
}

/**
 * lock the slot for a given key in a hash table
 */
static int ht_slot_lock(struct sip_msg* msg, char* key, char* foo)
{
	ht_pv_t *hpv;
	str skey;
	pv_spec_t *sp;
	unsigned int hid;
	unsigned int idx;

	sp = (pv_spec_t*)key;

	hpv = (ht_pv_t*)sp->pvp.pvn.u.dname;

	if(hpv->ht==NULL)
	{
		hpv->ht = ht_get_table(&hpv->htname);
		if(hpv->ht==NULL) {
			LM_ERR("cannot get $ht root\n");
			return -11;
		}
	}
	if(pv_printf_s(msg, hpv->pve, &skey)!=0)
	{
		LM_ERR("cannot get $ht key\n");
		return -1;
	}

	hid = ht_compute_hash(&skey);

	idx = ht_get_entry(hid, hpv->ht->htsize);

	LM_DBG("unlocking slot %.*s[%u] for key %.*s\n",
			hpv->htname.len, hpv->htname.s,
			idx, skey.len, skey.s);

	lock_get(&hpv->ht->entries[idx].lock);

	return 1;
}

/**
 * unlock the slot for a given key in a hash table
 */
static int ht_slot_unlock(struct sip_msg* msg, char* key, char* foo)
{
	ht_pv_t *hpv;
	str skey;
	pv_spec_t *sp;
	unsigned int hid;
	unsigned int idx;

	sp = (pv_spec_t*)key;

	hpv = (ht_pv_t*)sp->pvp.pvn.u.dname;

	if(hpv->ht==NULL)
	{
		hpv->ht = ht_get_table(&hpv->htname);
		if(hpv->ht==NULL) {
			LM_ERR("cannot get $ht root\n");
			return -11;
		}
	}
	if(pv_printf_s(msg, hpv->pve, &skey)!=0)
	{
		LM_ERR("cannot get $ht key\n");
		return -1;
	}

	hid = ht_compute_hash(&skey);

	idx = ht_get_entry(hid, hpv->ht->htsize);

	LM_DBG("unlocking slot %.*s[%u] for key %.*s\n",
			hpv->htname.len, hpv->htname.s,
			idx, skey.len, skey.s);

	lock_release(&hpv->ht->entries[idx].lock);

	return 1;
}

int ht_param(modparam_t type, void *val)
{
	if(val==NULL)
		goto error;

	return ht_table_spec((char*)val);
error:
	return -1;

}

#define MI_ERR_RELOAD 			"ERROR Reloading data"
#define MI_ERR_RELOAD_LEN 		(sizeof(MI_ERR_RELOAD)-1)
static struct mi_root* ht_mi_reload(struct mi_root* cmd_tree, void* param)
{
	struct mi_node* node;
	str htname;
	ht_t *ht;
	ht_t nht;
	ht_cell_t *first;
	ht_cell_t *it;
	int i;

	if(ht_db_url.len<=0)
		return init_mi_tree(500, MI_ERR_RELOAD, MI_ERR_RELOAD_LEN);
	
	if(ht_db_init_con()!=0)
		return init_mi_tree(500, MI_ERR_RELOAD, MI_ERR_RELOAD_LEN);
	if(ht_db_open_con()!=0)
		return init_mi_tree(500, MI_ERR_RELOAD, MI_ERR_RELOAD_LEN);

	node = cmd_tree->node.kids;
	if(node == NULL)
	{
		ht_db_close_con();
		return init_mi_tree( 400, MI_MISSING_PARM_S, MI_MISSING_PARM_LEN);
	}
	htname = node->value;
	if(htname.len<=0 || htname.s==NULL)
	{
		LM_ERR("bad hash table name\n");
		ht_db_close_con();
		return init_mi_tree( 500, "bad hash table name", 19);
	}
	ht = ht_get_table(&htname);
	if(ht==NULL || ht->dbtable.len<=0)
	{
		LM_ERR("bad hash table name\n");
		ht_db_close_con();
		return init_mi_tree( 500, "no such hash table", 18);
	}
	memcpy(&nht, ht, sizeof(ht_t));
	nht.entries = (ht_entry_t*)shm_malloc(nht.htsize*sizeof(ht_entry_t));
	if(nht.entries == NULL)
	{
		ht_db_close_con();
		return init_mi_tree(500, MI_ERR_RELOAD, MI_ERR_RELOAD_LEN);
	}
	memset(nht.entries, 0, nht.htsize*sizeof(ht_entry_t));

	if(ht_db_load_table(&nht, &ht->dbtable, 0)<0)
	{
		ht_db_close_con();
		return init_mi_tree(500, MI_ERR_RELOAD, MI_ERR_RELOAD_LEN);
	}

	/* replace old entries */
	for(i=0; i<nht.htsize; i++)
	{
		lock_get(&ht->entries[i].lock);
		first = ht->entries[i].first;
		ht->entries[i].first = nht.entries[i].first;
		ht->entries[i].esize = nht.entries[i].esize;
		lock_release(&ht->entries[i].lock);
		nht.entries[i].first = first;
	}
	/* free old entries */
	for(i=0; i<nht.htsize; i++)
	{
		first = nht.entries[i].first;
		while(first)
		{
			it = first;
			first = first->next;
			ht_cell_free(it);
		}
	}
	ht_db_close_con();
	return init_mi_tree( 200, MI_OK_S, MI_OK_LEN);
}

static struct mi_root* ht_mi_delete(struct mi_root* cmd_tree, void* param) {
	struct mi_node *node;
	str *htname, *key;
	ht_t *ht;

	node = cmd_tree->node.kids;
	if (!node)
		goto param_err;

	htname = &node->value;
	if (!htname->len)
		goto param_err;

	node = node->next;
	if (!node)
		goto param_err;

	key = &node->value;
	if (!key->len)
		goto param_err;

	ht = ht_get_table(htname);
	if (!ht)
		return init_mi_tree(404, MI_BAD_PARM_S, MI_BAD_PARM_LEN);

	ht_del_cell(ht, key);

	return init_mi_tree(200, MI_OK_S, MI_OK_LEN);

param_err:
	return init_mi_tree(400, MI_MISSING_PARM_S, MI_MISSING_PARM_LEN);
}

static struct mi_root* ht_mi_dump(struct mi_root* cmd_tree, void* param)
{
	struct mi_node* node;
	struct mi_node* node2;
	struct mi_root *rpl_tree;
	struct mi_node *rpl;
	str htname;
	ht_t *ht;
	ht_cell_t *it;
	int i;
	int len;
	char *p;

	node = cmd_tree->node.kids;
	if(node == NULL)
		return init_mi_tree( 400, MI_MISSING_PARM_S, MI_MISSING_PARM_LEN);
	htname = node->value;
	if(htname.len<=0 || htname.s==NULL)
	{
		LM_ERR("bad hash table name\n");
		return init_mi_tree( 500, "bad hash table name", 19);
	}
	ht = ht_get_table(&htname);
	if(ht==NULL)
	{
		LM_ERR("bad hash table name\n");
		return init_mi_tree( 500, "no such hash table", 18);
	}

	rpl_tree = init_mi_tree( 200, MI_OK_S, MI_OK_LEN);
	if (rpl_tree==NULL)
		return 0;
	rpl = &rpl_tree->node;

	for(i=0; i<ht->htsize; i++)
	{
		lock_get(&ht->entries[i].lock);
		it = ht->entries[i].first;
		if(it)
		{
			/* add entry node */
			p = int2str((unsigned long)i, &len);
			node = add_mi_node_child(rpl, MI_DUP_VALUE, "Entry", 5, p, len);
			if (node==0)
				goto error;
			while(it)
			{
				if(it->flags&AVP_VAL_STR) {
					node2 = add_mi_node_child(node, MI_DUP_VALUE, it->name.s, it->name.len,
							it->value.s.s, it->value.s.len);
				} else {
					p = sint2str((long)it->value.n, &len);
					node2 = add_mi_node_child(node, MI_DUP_VALUE, it->name.s, it->name.len,
							p, len);
				}
				if (node2==0)
					goto error;
				it = it->next;
			}
		}
		lock_release(&ht->entries[i].lock);
	}

	return rpl_tree;
error:
	free_mi_tree(rpl_tree);
	return 0;
}

static const char* htable_dump_doc[2] = {
	"Dump the contents of hash table.",
	0
};
static const char* htable_delete_doc[2] = {
	"Delete one key from a hash table.",
	0
};
static const char* htable_get_doc[2] = {
	"Get one key from a hash table.",
	0
};
static const char* htable_sets_doc[2] = {
	"Set one key in a hash table to a string value.",
	0
};
static const char* htable_seti_doc[2] = {
	"Set one key in a hash table to an integer value.",
	0
};
static const char* htable_list_doc[2] = {
	"List all htables.",
	0
};
static const char* htable_stats_doc[2] = {
	"Statistics about htables.",
	0
};
static const char* htable_reload_doc[2] = {
	"Reload hash table.",
	0
};

static void htable_rpc_delete(rpc_t* rpc, void* c) {
	str htname, keyname;
	ht_t *ht;

	if (rpc->scan(c, "SS", &htname, &keyname) < 2) {
		rpc->fault(c, 500, "Not enough parameters (htable name & key name");
		return;
	}
	ht = ht_get_table(&htname);
	if (!ht) {
		rpc->fault(c, 500, "No such htable");
		return;
	}

	ht_del_cell(ht, &keyname);
}

/*! \brief RPC htable.get command to get one item */
static void htable_rpc_get(rpc_t* rpc, void* c) {
	str htname, keyname;
	ht_t *ht;
	ht_cell_t *htc;	/*!< One HT cell */
	void* th;
	void* vh;

	if (rpc->scan(c, "SS", &htname, &keyname) < 2) {
		rpc->fault(c, 500, "Not enough parameters (htable name and key name)");
		return;
	}

	/* Find the htable */
	ht = ht_get_table(&htname);
	if (!ht) {
		rpc->fault(c, 500, "No such htable");
		return;
	}

	/* Find the  cell */
	htc = ht_cell_pkg_copy(ht, &keyname, NULL);
	if(htc == NULL) {
		/* Print error message */
		rpc->fault(c, 500, "Key name doesn't exist in htable.");
		return;
	}

	/* add entry node */
	if (rpc->add(c, "{", &th) < 0) {
		rpc->fault(c, 500, "Internal error creating rpc");
		goto error;
	}

	if(rpc->struct_add(th, "{", "item", &vh)<0) {
		rpc->fault(c, 500, "Internal error creating rpc");
		goto error;
	}

	if(htc->flags&AVP_VAL_STR) {
		if(rpc->struct_add(vh, "SS", "name",  &htc->name.s, "value", &htc->value.s)<0)
		{
			rpc->fault(c, 500, "Internal error adding item");
			goto error;
		}
	} else {
		if(rpc->struct_add(vh, "Sd", "name",  &htc->name.s, "value", (int)htc->value.n))
		{
			rpc->fault(c, 500, "Internal error adding item");
			goto error;
		}
	}
	
error:
	/* Release the allocated memory */
	ht_cell_pkg_free(htc);

	return;
}

/*! \brief RPC htable.sets command to set one item to string value */
static void htable_rpc_sets(rpc_t* rpc, void* c) {
	str htname, keyname;
	int_str keyvalue;
	ht_t *ht;

	if (rpc->scan(c, "SS.S", &htname, &keyname, &keyvalue.s) < 3) {
		rpc->fault(c, 500,
				"Not enough parameters (htable name, key name and value)");
		return;
	}

	/* Find the htable */
	ht = ht_get_table(&htname);
	if (!ht) {
		rpc->fault(c, 500, "No such htable");
		return;
	}
	
	if(ht_set_cell(ht, &keyname, AVP_VAL_STR, &keyvalue, 1)!=0)
	{
		LM_ERR("cannot set $ht(%.*s=>%.*s)\n", htname.len, htname.s,
				keyname.len, keyname.s);
		rpc->fault(c, 500, "Failed to set the item");
		return;
	}

	return;
}

/*! \brief RPC htable.seti command to set one item to integer value */
static void htable_rpc_seti(rpc_t* rpc, void* c) {
	str htname, keyname;
	int_str keyvalue;
	ht_t *ht;

	if (rpc->scan(c, "SS.d", &htname, &keyname, &keyvalue.n) < 3) {
		rpc->fault(c, 500,
				"Not enough parameters (htable name, key name and value)");
		return;
	}

	/* Find the htable */
	ht = ht_get_table(&htname);
	if (!ht) {
		rpc->fault(c, 500, "No such htable");
		return;
	}
	
	if(ht_set_cell(ht, &keyname, 0, &keyvalue, 1)!=0)
	{
		LM_ERR("cannot set $ht(%.*s=>%.*s)\n", htname.len, htname.s,
				keyname.len, keyname.s);
		rpc->fault(c, 500, "Failed to set the item");
		return;
	}

	return;
}

/*! \brief RPC htable.dump command to print content of a hash table */
static void  htable_rpc_dump(rpc_t* rpc, void* c)
{
	str htname;
	ht_t *ht;
	ht_cell_t *it;
	int i;
	void* th;
	void* ih;
	void* vh;

	if (rpc->scan(c, "S", &htname) < 1)
	{
		rpc->fault(c, 500, "No htable name given");
		return;
	}
	ht = ht_get_table(&htname);
	if(ht==NULL)
	{
		rpc->fault(c, 500, "No such htable");
		return;
	}
	for(i=0; i<ht->htsize; i++)
	{
		lock_get(&ht->entries[i].lock);
		it = ht->entries[i].first;
		if(it)
		{
			/* add entry node */
			if (rpc->add(c, "{", &th) < 0)
			{
				rpc->fault(c, 500, "Internal error creating rpc");
				goto error;
			}
			if(rpc->struct_add(th, "dd{",
							"entry", i,
							"size",  (int)ht->entries[i].esize,
							"slot",  &ih)<0)
			{
				rpc->fault(c, 500, "Internal error creating rpc");
				goto error;
			}
			while(it)
			{
				if(rpc->struct_add(ih, "{",
							"item", &vh)<0)
				{
					rpc->fault(c, 500, "Internal error creating rpc");
					goto error;
				}
				if(it->flags&AVP_VAL_STR) {
					if(rpc->struct_add(vh, "SS",
							"name",  &it->name.s,
							"value", &it->value.s)<0)
					{
						rpc->fault(c, 500, "Internal error adding item");
						goto error;
					}
				} else {
					if(rpc->struct_add(vh, "Sd",
							"name",  &it->name.s,
							"value", (int)it->value.n))
					{
						rpc->fault(c, 500, "Internal error adding item");
						goto error;
					}
				}
				it = it->next;
			}
		}
		lock_release(&ht->entries[i].lock);
	}

	return;

error:
	lock_release(&ht->entries[i].lock);
}

static void  htable_rpc_list(rpc_t* rpc, void* c)
{
	ht_t *ht;
	void* th;
	char dbname[128];

	ht = ht_get_root();
	if(ht==NULL)
	{
		rpc->fault(c, 500, "No htables");
		return;
	}
	while (ht != NULL)
	{
		int len = 0;
		/* add entry node */
		if (rpc->add(c, "{", &th) < 0)
		{
			rpc->fault(c, 500, "Internal error creating structure rpc");
			goto error;
		}
		if (ht->dbtable.len > 0) {
			len = ht->dbtable.len > 127 ? 127 : ht->dbtable.len;
			memcpy(dbname, ht->dbtable.s, len);
			dbname[ht->dbtable.len] = '\0';
		} else {
			dbname[0] = '\0';
		}

		if(rpc->struct_add(th, "Ssdddd",
						"name", &ht->name,	/* String */
						"dbtable", &dbname ,	/* Char * */
						"dbmode", (int)  ht->dbmode,		/* u int */
						"expire", (int) ht->htexpire,		/* u int */
						"updateexpire", ht->updateexpire,	/* int */
						"size", (int) ht->htsize		/* u int */
						) < 0) {
			rpc->fault(c, 500, "Internal error creating data rpc");
			goto error;
		}
		ht = ht->next;
	}

error:
	return;
}

static void  htable_rpc_stats(rpc_t* rpc, void* c)
{
	ht_t *ht;
	void* th;
	unsigned int min;
	unsigned int max;
	unsigned int all;
	unsigned int i;

	ht = ht_get_root();
	if(ht==NULL)
	{
		rpc->fault(c, 500, "No htables");
		return;
	}
	while (ht != NULL)
	{
		/* add entry node */
		if (rpc->add(c, "{", &th) < 0)
		{
			rpc->fault(c, 500, "Internal error creating structure rpc");
			goto error;
		}
		all = 0;
		max = 0;
		min = 4294967295U;
		for(i=0; i<ht->htsize; i++) {
			lock_get(&ht->entries[i].lock);
			if(ht->entries[i].esize<min)
				min = ht->entries[i].esize;
			if(ht->entries[i].esize>max)
				max = ht->entries[i].esize;
			all += ht->entries[i].esize;
			lock_release(&ht->entries[i].lock);
		}

		if(rpc->struct_add(th, "Sddd",
						"name", &ht->name,	/* str */
						"slots", (int)ht->htsize,	/* uint */
						"all", (int)all,	/* uint */
						"min", (int)min,	/* uint */
						"max", (int)max		/* uint */
						) < 0) {
			rpc->fault(c, 500, "Internal error creating rpc structure");
			goto error;
		}
		ht = ht->next;
	}

error:
	return;
}


/*! \brief RPC htable.reload command to reload content of a hash table */
static void htable_rpc_reload(rpc_t* rpc, void* c)
{
	str htname;
	ht_t *ht;
	ht_t nht;
	ht_cell_t *first;
	ht_cell_t *it;
	int i;

	if(ht_db_url.len<=0) {
		rpc->fault(c, 500, "No htable db_url");
		return;
	}
	if(ht_db_init_con()!=0) {
		rpc->fault(c, 500, "Failed to init htable db connection");
		return;
	}
	if(ht_db_open_con()!=0) {
		rpc->fault(c, 500, "Failed to open htable db connection");
		return;
	}

	if (rpc->scan(c, "S", &htname) < 1)
	{
		rpc->fault(c, 500, "No htable name given");
		return;
	}
	ht = ht_get_table(&htname);
	if(ht==NULL)
	{
		rpc->fault(c, 500, "No such htable");
		return;
	}


	memcpy(&nht, ht, sizeof(ht_t));
	nht.entries = (ht_entry_t*)shm_malloc(nht.htsize*sizeof(ht_entry_t));
	if(nht.entries == NULL)
	{
		ht_db_close_con();
		rpc->fault(c, 500, "Mtree reload failed");
		return;
	}
	memset(nht.entries, 0, nht.htsize*sizeof(ht_entry_t));

	if(ht_db_load_table(&nht, &ht->dbtable, 0)<0)
	{
		ht_db_close_con();
		rpc->fault(c, 500, "Mtree reload failed");
		return;
	}

	/* replace old entries */
	for(i=0; i<nht.htsize; i++)
	{
		lock_get(&ht->entries[i].lock);
		first = ht->entries[i].first;
		ht->entries[i].first = nht.entries[i].first;
		ht->entries[i].esize = nht.entries[i].esize;
		lock_release(&ht->entries[i].lock);
		nht.entries[i].first = first;
	}
	/* free old entries */
	for(i=0; i<nht.htsize; i++)
	{
		first = nht.entries[i].first;
		while(first)
		{
			it = first;
			first = first->next;
			ht_cell_free(it);
		}
	}
	ht_db_close_con();
	return;
}

rpc_export_t htable_rpc[] = {
	{"htable.dump", htable_rpc_dump, htable_dump_doc, 0},
	{"htable.delete", htable_rpc_delete, htable_delete_doc, 0},
	{"htable.get", htable_rpc_get, htable_get_doc, 0},
	{"htable.sets", htable_rpc_sets, htable_sets_doc, 0},
	{"htable.seti", htable_rpc_seti, htable_seti_doc, 0},
	{"htable.listTables", htable_rpc_list, htable_list_doc, 0},
	{"htable.reload", htable_rpc_reload, htable_reload_doc, 0},
	{"htable.stats", htable_rpc_stats, htable_stats_doc, 0},
	{0, 0, 0, 0}
};

static int htable_init_rpc(void)
{
	if (rpc_register_array(htable_rpc)!=0)
	{
		LM_ERR("failed to register RPC commands\n");
		return -1;
	}
	return 0;
}
