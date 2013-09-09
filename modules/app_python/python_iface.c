/* $Id$
 *
 * Copyright (C) 2009 Sippy Software, Inc., http://www.sippysoft.com
 *
 * This file is part of SIP-Router, a free SIP server.
 *
 * SIP-Router is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * SIP-Router is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/

/*
 * Changed
 *	2012-12-10 ez: Moved a part of functional to mod_Core.c, mod_Logger.c, mod_Ranks.c, mod_Router.c
 *
*/

// Python includes
#include <Python.h>

// router includes
#include "../../action.h"
#include "../../dprint.h"
#include "../../route_struct.h"
#include "../../str.h"
#include "../../sr_module.h"

// local includes
#include "mod_Router.h"
#include "mod_Core.h"
#include "mod_Ranks.h"
#include "mod_Logger.h"


int init_modules(void)
{
    init_mod_Router();
    init_mod_Core();
    init_mod_Ranks();
    init_mod_Logger();

    return 0;
}

