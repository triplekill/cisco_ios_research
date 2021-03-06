/* $Id: tbridge_debug.c,v 3.2.62.2 1996/07/25 09:55:14 snyder Exp $
 * $Source: /release/112/cvs/Xsys/tbridge/tbridge_debug.c,v $
 *------------------------------------------------------------------
 * Transparent Bridging debug facility.
 *
 * May 1995, Tony Speakman
 *
 * Copyright (c) 1995-1997 by cisco Systems, Inc.
 * All rights reserved.
 *------------------------------------------------------------------
 * $Log: tbridge_debug.c,v $
 * Revision 3.2.62.2  1996/07/25  09:55:14  snyder
 * CSCdi63981:  eliminate unused code, fix bugs, make a common routine
 *              1. create a routine call generic_debug_all and
 *                 make most debugs use it
 *              2. eliminate if (*_debug_inited) return, most never set
 *              the var
 *                 to TRUE making the code useless.
 *              3. declare some constant arrays const
 *              4. fix bugs found along the way
 *              5. save 2768 bytes from image, 660 out of data section
 * Branch: California_branch
 *
 * Revision 3.2.62.1  1996/03/18  22:12:09  gstovall
 * Branch: California_branch
 * Elvis has left the building.  He headed out to California, and took the
 * port ready changes with him.
 *
 * Revision 3.2.26.2  1996/03/07  10:51:55  mdb
 * Branch: DeadKingOnAThrone_branch
 * cisco and ANSI/POSIX libraries.
 *
 * Revision 3.2.26.1  1996/02/20  18:52:25  dstine
 * Branch: DeadKingOnAThrone_branch
 *           Sync from DeadKingOnAThrone_baseline_960122 to
 *                     DeadKingOnAThrone_baseline_960213
 *
 * Revision 3.2  1995/11/17  18:45:00  hampton
 * Remove old entries from the RCS header logs.
 *
 * Revision 3.1  1995/11/09  13:33:22  shaker
 * Bump version numbers from 2.x to 3.x.
 *
 * Revision 2.1  1995/06/07  23:03:04  hampton
 * Bump version numbers from 1.x to 2.x.
 *
 *------------------------------------------------------------------
 * $Endlog$
 */

#include "master.h"
#include "sys_registry.h"
#include "config.h"
#include "interface_private.h"
#include "../ibm/netbios.h"
#include "../ui/debug.h"
#include "tbridge_debug.h"

/* Declare the initialized text for the debugging array */
#define __DECLARE_DEBUG_ARR__
#include "tbridge_debug_flags.h"


/*
 * tbridge_debug_command
 * Handles debug commands related to TBRIDGE
 */

void tbridge_debug_command (parseinfo *csb)
{
    /* If there's no special case, use the generic handler */
    debug_command(csb);
}

/*
 * tbridge_debug_all is registered to be called whenever anybody issues
 * a "debug all" or "undebug all" command... or whenever you want to
 * set the state of all the TBRIDGE debug flags at once. The argument is
 * TRUE for "debug all", FALSE for "undebug all".
 */

static void tbridge_debug_all (boolean flag)
{
    generic_debug_all(tbridge_debug_arr, flag);
}

/*
 * tbridge_debug_show is called to display the values of all the TBRIDGE
 * debugging variables.
 */

static void tbridge_debug_show (void)
{
    debug_show_flags(&(tbridge_debug_arr[0]), "Transparent Bridging");
}
	

/*
 * tbridge_debug_init is called at TBRIDGE subsystem startup. It registers
 * the routines to maintain and display the TBRIDGE debug flags, and
 * makes sure the flags are set to the appropriate values depending on
 * whether "debug all" is in effect when TBRIDGE is started.
 */

void tbridge_debug_init (void)
{

    /* Register for "debug all" and "show debug" events. A Real Programmer
     * would be prepared to do something with the error code returned
     * from registry_add_list. Oh, well...
     */
    reg_add_debug_all(tbridge_debug_all, "tbridge_debug_all");
    reg_add_debug_show(tbridge_debug_show, "tbridge_debug_show");

    /* Make sure the debug flags are right at startup. If "debug all"
     * is in effect when TBRIDGE is initialized, we want to turn on
     * all the TBRIDGE debugging right now.
     */
    tbridge_debug_all(debug_all_p());
}
