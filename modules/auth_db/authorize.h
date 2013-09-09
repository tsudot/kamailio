/*
 * $Id$
 *
 * Digest Authentication - Database support
 *
 * Copyright (C) 2001-2003 FhG Fokus
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


#ifndef AUTHORIZE_H
#define AUTHORIZE_H


#include "../../parser/msg_parser.h"
#include "api.h"

int auth_db_init(const str* db_url);
int auth_db_bind(const str* db_url);
void auth_db_close(void);

/*
 * Authorize using Proxy-Authorization header field
 */
int proxy_authenticate(struct sip_msg* _msg, char* _realm, char* _table);


/*
 * Authorize using WWW-Authorization header field
 */
int www_authenticate(struct sip_msg* _msg, char* _realm, char* _table);
int www_authenticate2(struct sip_msg* _msg, char* _realm, char* _table, char *_method);

/*
 * Authenticate using WWW/Proxy-Authorize header field
 */
int auth_check(struct sip_msg* _m, char* _realm, char* _table, char *_flags);

/*
 * Fetch credentials for a specific user
 */
int fetch_credentials(sip_msg_t *msg, str *user, str* domain, str *table);

/*
 * Bind to AUTH_DB API
 */
int bind_auth_db(auth_db_api_t* api);

#endif /* AUTHORIZE_H */
