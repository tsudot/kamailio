/**
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


#ifndef _BIND_DMQ_H_
#define _BIND_DMQ_H_

#include "peer.h"
#include "dmqnode.h"
#include "dmq_funcs.h"

typedef int (*bcast_message_t)(dmq_peer_t* peer, str* body, dmq_node_t* except,
		dmq_resp_cback_t* resp_cback, int max_forwards);
typedef int (*send_message_t)(dmq_peer_t* peer, str* body, dmq_node_t* node,
		dmq_resp_cback_t* resp_cback, int max_forwards);

typedef struct dmq_api {
	register_dmq_peer_t register_dmq_peer;
	bcast_message_t bcast_message;
	send_message_t send_message;
} dmq_api_t;

typedef int (*bind_dmq_f)(dmq_api_t* api);

int bind_dmq(dmq_api_t* api);

#endif

