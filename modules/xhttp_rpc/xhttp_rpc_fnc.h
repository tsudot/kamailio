/*
 * $Id$
 *
 * Copyright (C) 2011 VoIP Embedded, Inc.
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
 * 2011-11-11  initial version (osas)
 */


#ifndef _XHTTP_RPC_FNC_H
#define _XHTTP_RPC_FNC_H


int xhttp_rpc_parse_url(str *url, int *mod, int *cmd, str *arg);
void xhttp_rpc_get_next_arg(rpc_ctx_t* ctx, str *arg);
int xhttp_rpc_build_content(rpc_ctx_t *ctx, str *val, str *id);
int xhttp_rpc_insert_break(rpc_ctx_t *ctx);
int xhttp_rpc_build_page(rpc_ctx_t *ctx);

#endif

