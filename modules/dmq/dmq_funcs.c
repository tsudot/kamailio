/*
 * $Id$
 *
 * dmq module - distributed message queue
 *
 * Copyright (C) 2011 Bucur Marius - Ovidiu
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

#include "dmq_funcs.h"
#include "notification_peer.h"

/**
 * @brief register a DMQ peer
 */
dmq_peer_t* register_dmq_peer(dmq_peer_t* peer)
{
	dmq_peer_t* new_peer;
	lock_get(&peer_list->lock);
	if(search_peer_list(peer_list, peer)) {
		LM_ERR("peer already exists: %.*s %.*s\n", peer->peer_id.len, peer->peer_id.s,
		       peer->description.len, peer->description.s);
		lock_release(&peer_list->lock);
		return NULL;
	}
	new_peer = add_peer(peer_list, peer);
	lock_release(&peer_list->lock);
	return new_peer;
}

/**
 * @brief dmq tm callback
 */
void dmq_tm_callback(struct cell *t, int type, struct tmcb_params *ps)
{
	dmq_cback_param_t* cb_param;
	
	cb_param = (dmq_cback_param_t*)(*ps->param);

	if(cb_param==NULL)
		return;

	LM_DBG("dmq_tm_callback start\n");
	if(cb_param->resp_cback.f) {
		if(cb_param->resp_cback.f(ps->rpl, ps->code, cb_param->node, cb_param->resp_cback.param) < 0) {
			LM_ERR("error in response callback\n");
		}
	}
	LM_DBG("dmq_tm_callback done\n");
	shm_free_node(cb_param->node);
	shm_free(cb_param);
	*ps->param = NULL;
}

int build_uri_str(str* username, struct sip_uri* uri, str* from)
{
	/* sip:user@host:port */
	int from_len;
	
	from_len = username->len + uri->host.len + uri->port.len + 10;
	if(!uri->host.s || !uri->host.len) {
		LM_ERR("no host in uri\n");
		return -1;
	}
	if(!username->s || !username->len) {
		LM_ERR("no username given\n");
		return -1;
	}
	from->s = pkg_malloc(from_len);
	if(from->s==NULL) {
		LM_ERR("no more pkg\n");
		return -1;
	}
	from->len = 0;
	
	memcpy(from->s, "sip:", 4);
	from->len += 4;
	
	memcpy(from->s + from->len, username->s, username->len);
	from->len += username->len;
	
	memcpy(from->s + from->len, "@", 1);
	from->len += 1;
	
	memcpy(from->s + from->len, uri->host.s, uri->host.len);
	from->len += uri->host.len;
	
	if(uri->port.s && uri->port.len) {
		memcpy(from->s + from->len, ":", 1);
		from->len += 1;
		memcpy(from->s + from->len, uri->port.s, uri->port.len);
		from->len += uri->port.len;
	}
	return 0;
}

/**
 * @brief broadcast a dmq message
 *
 * peer - the peer structure on behalf of which we are sending
 * body - the body of the message
 * except - we do not send the message to this node
 * resp_cback - a response callback that gets called when the transaction is complete
 */
int bcast_dmq_message(dmq_peer_t* peer, str* body, dmq_node_t* except,
		dmq_resp_cback_t* resp_cback, int max_forwards)
{
	dmq_node_t* node;
	
	lock_get(&node_list->lock);
	node = node_list->nodes;
	while(node) {
		/* we do not send the message to the following:
		 *   - the except node
		 *   - itself
		 *   - any inactive nodes
		 */
		if((except && cmp_dmq_node(node, except)) || node->local
				|| node->status != DMQ_NODE_ACTIVE) {
			LM_DBG("skipping node %.*s\n", STR_FMT(&node->orig_uri));
			node = node->next;
			continue;
		}
		if(dmq_send_message(peer, body, node, resp_cback, max_forwards) < 0) {
			LM_ERR("error sending dmq message\n");
			goto error;
		}
		node = node->next;
	}
	lock_release(&node_list->lock);
	return 0;
error:
	lock_release(&node_list->lock);
	return -1;
}

/**
 * @brief send a dmq message
 *
 * peer - the peer structure on behalf of which we are sending
 * body - the body of the message
 * node - we send the message to this node
 * resp_cback - a response callback that gets called when the transaction is complete
 */
int dmq_send_message(dmq_peer_t* peer, str* body, dmq_node_t* node,
		dmq_resp_cback_t* resp_cback, int max_forwards)
{
	uac_req_t uac_r;
	str str_hdr = {0, 0};
	str from, to, req_uri;
	dmq_cback_param_t* cb_param = NULL;
	int result = 0;
	int len = 0;
	
	/* Max-Forwards */
	str_hdr.len = 20 + CRLF_LEN;
	str_hdr.s = pkg_malloc(str_hdr.len);
	if(str_hdr.s==NULL) {
		LM_ERR("no more pkg\n");
		return -1;
	}
	len += sprintf(str_hdr.s, "Max-Forwards: %d%s", max_forwards, CRLF);
	str_hdr.len = len;
	
	cb_param = shm_malloc(sizeof(*cb_param));
	memset(cb_param, 0, sizeof(*cb_param));
	cb_param->resp_cback = *resp_cback;
	cb_param->node = shm_dup_node(node);
	
	if(build_uri_str(&peer->peer_id, &dmq_server_uri, &from) < 0) {
		LM_ERR("error building from string [username %.*s]\n",
				STR_FMT(&peer->peer_id));
		goto error;
	}
	if(build_uri_str(&peer->peer_id, &node->uri, &to) < 0) {
		LM_ERR("error building to string\n");
		goto error;
	}
	req_uri = to;
	
	set_uac_req(&uac_r, &dmq_request_method, &str_hdr, body, NULL,
			TMCB_LOCAL_COMPLETED, dmq_tm_callback, (void*)cb_param);
	result = tmb.t_request(&uac_r, &req_uri,
			       &to, &from,
			       NULL);
	if(result < 0) {
		LM_ERR("error in tmb.t_request_within\n");
		goto error;
	}
	pkg_free(str_hdr.s);
	return 0;
error:
	pkg_free(str_hdr.s);
	return -1;
}

/**
 * @brief config file function for sending dmq message
 */
int cfg_dmq_send_message(struct sip_msg* msg, char* peer, char* to, char* body)
{
	str peer_str;
	str to_str;
	str body_str;
	
	if(get_str_fparam(&peer_str, msg, (fparam_t*)peer)<0) {
		LM_ERR("cannot get peer value\n");
		return -1;
	}
	if(get_str_fparam(&to_str, msg, (fparam_t*)to)<0) {
		LM_ERR("cannot get dst value\n");
		return -1;
	}
	if(get_str_fparam(&body_str, msg, (fparam_t*)body)<0) {
		LM_ERR("cannot get body value\n");
		return -1;
	}
	
	LM_INFO("cfg_dmq_send_message: %.*s - %.*s - %.*s\n",
		peer_str.len, peer_str.s,
		to_str.len, to_str.s,
		body_str.len, body_str.s);
	
	dmq_peer_t* destination_peer = find_peer(peer_str);
	if(!destination_peer) {
		LM_INFO("cannot find peer %.*s\n", peer_str.len, peer_str.s);
		dmq_peer_t new_peer;
		new_peer.callback = empty_peer_callback;
		new_peer.description.s = "";
		new_peer.description.len = 0;
		new_peer.peer_id = peer_str;
		destination_peer = register_dmq_peer(&new_peer);
		if(!destination_peer) {
			LM_ERR("error in register_dmq_peer\n");
			goto error;
		}
	}
	dmq_node_t* to_dmq_node = find_dmq_node_uri(node_list, &to_str);
	if(!to_dmq_node) {
		LM_ERR("cannot find dmq_node: %.*s\n", to_str.len, to_str.s);
		goto error;
	}
	if(dmq_send_message(destination_peer, &body_str, to_dmq_node,
				&notification_callback, 1) < 0) {
		LM_ERR("cannot send dmq message\n");
		goto error;
	}
	return 1;
error:
	return -1;
}

/**
 * @brief pings the servers in the nodelist
 *
 * if the server does not reply to the ping, it is removed from the list
 * the ping messages are actualy notification requests
 * this way the ping will have two uses:
 *   - checks if the servers in the list are up and running
 *   - updates the list of servers from the other nodes
 */
void ping_servers(unsigned int ticks, void *param) {
	str* body;
	int ret;
	LM_DBG("ping_servers\n");
	body = build_notification_body();
	ret = bcast_dmq_message(dmq_notification_peer, body, notification_node,
			&notification_callback, 1);
	pkg_free(body->s);
	pkg_free(body);
	if(ret < 0) {
		LM_ERR("error broadcasting message\n");
	}
}

