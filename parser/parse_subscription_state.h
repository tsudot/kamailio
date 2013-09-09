/*
 * Copyright (C) 2006 Vaclav Kubart, vaclav dot kubart at iptel dot org
 *
 * This file is part of SIP-router, a free SIP server.
 *
 * SIP-router is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * SIP-router is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*! \file
 * \brief Parser :: Parse subscription-state in NOTIFY
 *
 * \ingroup parser
 */

#ifndef __PARSE_SUBSCRIPTION_STATE_H
#define __PARSE_SUBSCRIPTION_STATE_H

#include "hf.h"

typedef enum {
	ss_active,
	ss_pending,
	ss_terminated,
	ss_extension
} substate_value_t;

typedef struct _subscription_state_t {
	substate_value_t value;
	unsigned int expires;
	int expires_set; /* expires is valid if nonzero here */
} subscription_state_t;

int parse_subscription_state(struct hdr_field *h);

void free_subscription_state(subscription_state_t **ss);

#endif
