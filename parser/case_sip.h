/*
 * Copyright (C) 2004 Jamey Hicks, jamey dot hicks at hp dot com
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
 * \brief Parser :: Handle case for headers
 *
 * \ingroup parser
 */


#ifndef CASE_SIP_H
#define CASE_SIP_H

#define atch_CASE                            \
        switch(LOWER_DWORD(val)) {          \
        case _atch_:                        \
		DBG("end of SIP-If-Match\n"); \
                hdr->type = HDR_SIPIFMATCH_T; \
                p += 4;                     \
                goto dc_end;                \
        }


#define ifm_CASE				\
	switch(LOWER_DWORD(val)) {		\
	case _ifm_:				\
		DBG("middle of SIP-If-Match: yet=0x%04x\n",LOWER_DWORD(val)); \
		p += 4;				\
		val = READ(p);			\
		atch_CASE;			\
		goto other;			\
	}
		
#define sip_CASE          \
	DBG("beginning of SIP-If-Match: yet=0x%04x\n",LOWER_DWORD(val)); \
        p += 4;           \
        val = READ(p);    \
        ifm_CASE;         \
        goto other;

#endif /* CASE_SIP_H */
