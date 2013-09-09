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

#include "third_party_reg.h"

/**
 * Handle third party registration
 * @param msg - the SIP REGISTER message
 * @param m  - the isc_match that matched with info about where to forward it 
 * @param mark  - the isc_mark that should be used to mark the message
 * @returns #ISC_RETURN_TRUE if allowed, #ISC_RETURN_FALSE if not
 */
int isc_third_party_reg(struct sip_msg *msg, isc_match *m, isc_mark *mark) {
	r_third_party_registration r;
	int expires = 0;
	str req_uri = { 0, 0 };
	str to = { 0, 0 };
	str pvni = { 0, 0 };
	str pani = { 0, 0 };
	str cv = { 0, 0 };

	struct hdr_field *hdr;

	LM_DBG("isc_third_party_reg: Enter\n");

	/* Set Request Uri to IFC matching server name */
	req_uri.len = m->server_name.len;
	req_uri.s = m->server_name.s;

	/* Get To header*/
	to = cscf_get_public_identity(msg);

	/*TODO - check if the min/max expires is in the acceptable limits
	 * this does not work correctly if the user has multiple contacts
	 * and register/deregisters them individually!!!
	 */
	expires = cscf_get_max_expires(msg, 0);

	/* Get P-Visited-Network-Id header */
	pvni = cscf_get_visited_network_id(msg, &hdr);
	/* Get P-Access-Network-Info header */
	pani = cscf_get_access_network_info(msg, &hdr);

	/* Get P-Charging-Vector header */
	/* Just forward the charging header received from P-CSCF */
	/* Todo: implement also according to TS 24.229, chap 5.4.1.7 */
	cv = cscf_get_charging_vector(msg, &hdr);

	if (req_uri.s) {

		memset(&r, 0, sizeof(r_third_party_registration));

		r.req_uri = req_uri;
		r.to = to;
		r.from = isc_my_uri_sip;
		r.pvni = pvni;
		r.pani = pani;
		r.cv = cv;
		r.service_info = m->service_info;

		if (expires <= 0)
			r_send_third_party_reg(&r, 0);
		else
			r_send_third_party_reg(&r, expires + isc_expires_grace);
		return ISC_RETURN_TRUE;
	} else {
		return ISC_RETURN_FALSE;
	}
}

static str method = { "REGISTER", 8 };
static str event_hdr = { "Event: registration\r\n", 21 };
static str max_fwds_hdr = { "Max-Forwards: 10\r\n", 18 };
static str expires_s = { "Expires: ", 9 };
static str expires_e = { "\r\n", 2 };
static str contact_s = { "Contact: <", 10 };
static str contact_e = { ">\r\n", 3 };

static str p_visited_network_id_s = { "P-Visited-Network-ID: ", 22 };
static str p_visited_network_id_e = { "\r\n", 2 };

static str p_access_network_info_s = { "P-Access-Network-Info: ", 23 };
static str p_access_network_info_e = { "\r\n", 2 };

static str p_charging_vector_s = { "P-Charging-Vector: ", 19 };
static str p_charging_vector_e = { "\r\n", 2 };
static str body_s = { "<ims-3gpp version=\"1\"><service-info>", 36 };
static str body_e = { "</service-info></ims-3gpp>", 26 };

/**
 * Send a third party registration
 * @param r - the register to send for
 * @param expires - expires time
 * @returns true if OK, false if not
 */

int r_send_third_party_reg(r_third_party_registration *r, int expires) {
	str h = { 0, 0 };
	str b = { 0, 0 };
	uac_req_t req;

	LM_DBG("r_send_third_party_reg: REGISTER to <%.*s>\n",
			r->req_uri.len, r->req_uri.s);

	h.len = event_hdr.len + max_fwds_hdr.len;
	h.len += expires_s.len + 12 + expires_e.len;

	h.len += contact_s.len + isc_my_uri_sip.len + contact_e.len;

	if (r->pvni.len)
		h.len += p_visited_network_id_s.len + p_visited_network_id_e.len
				+ r->pvni.len;

	if (r->pani.len)
		h.len += p_access_network_info_s.len + p_access_network_info_e.len
				+ r->pani.len;

	if (r->cv.len)
		h.len += p_charging_vector_s.len + p_charging_vector_e.len + r->cv.len;

	h.s = pkg_malloc(h.len);
	if (!h.s) {
		LM_ERR("r_send_third_party_reg: Error allocating %d bytes\n", h.len);
		h.len = 0;
		return 0;
	}

	h.len = 0;
	STR_APPEND(h, event_hdr);

	STR_APPEND(h, max_fwds_hdr);

	STR_APPEND(h, expires_s);
	sprintf(h.s + h.len, "%d", expires);
	h.len += strlen(h.s + h.len);
	STR_APPEND(h, expires_e);

	STR_APPEND(h, contact_s);
	STR_APPEND(h, isc_my_uri_sip);
	STR_APPEND(h, contact_e);

	if (r->pvni.len) {
		STR_APPEND(h, p_visited_network_id_s);
		STR_APPEND(h, r->pvni);
		STR_APPEND(h, p_visited_network_id_e);
	}

	if (r->pani.len) {
		STR_APPEND(h, p_access_network_info_s);
		STR_APPEND(h, r->pani);
		STR_APPEND(h, p_access_network_info_e);
	}

	if (r->cv.len) {
		STR_APPEND(h, p_charging_vector_s);
		STR_APPEND(h, r->cv);
		STR_APPEND(h, p_charging_vector_e);
	}
	LM_CRIT("SRV INFO:<%.*s>\n", r->service_info.len, r->service_info.s);
	if (r->service_info.len) {
		b.len = body_s.len + r->service_info.len + body_e.len;
		b.s = pkg_malloc(b.len);
		if (!b.s) {
			LM_ERR("r_send_third_party_reg: Error allocating %d bytes\n", b.len);
			b.len = 0;
			return 0;
		}

		b.len = 0;
		STR_APPEND(b, body_s);
		STR_APPEND(b, r->service_info);
		STR_APPEND(b, body_e);
	}

	set_uac_req(&req, &method, &h, &b, 0,
			TMCB_RESPONSE_IN | TMCB_ON_FAILURE | TMCB_LOCAL_COMPLETED,
			r_third_party_reg_response, &(r->req_uri));
	if (isc_tmb.t_request(&req, &(r->req_uri), &(r->to), &(r->from), 0) < 0) {
		LM_ERR("r_send_third_party_reg: Error sending in transaction\n");
		goto error;
	}
	if (h.s)
		pkg_free(h.s);
	return 1;

	error: if (h.s)
		pkg_free(h.s);
	return 0;
}

/**
 * Response callback for third party register
 */
void r_third_party_reg_response(struct cell *t, int type, struct tmcb_params *ps) {
	LM_DBG("r_third_party_reg_response: code %d\n", ps->code);
	if (!ps->rpl) {
		LM_ERR("r_third_party_reg_response: No reply\n");
		return;
	}

	if (ps->code >= 200 && ps->code < 300) {
		if (ps->rpl)
			cscf_get_expires_hdr(ps->rpl, 0);
		else
			return;
	} else if (ps->code == 404) {
	} else {
		LM_DBG("r_third_party_reg_response: code %d\n", ps->code);
	}
}

