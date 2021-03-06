/************************************************************************
 *                                                                      *
 *                              NOTE WELL                               *
 * This is vendor-supplied and vendor-supported code.  Do not make any  *
 * modifications to this code without the knowledge and consent of the  *
 * SNMP agent group.                                                    *
 *                                                                      *
 ************************************************************************/
/* $Id: sr_k_hist.c,v 3.4.12.2 1996/07/01 23:48:15 snyder Exp $
 * $Source: /release/112/cvs/Xsys/rmon/sr_k_hist.c,v $
 *------------------------------------------------------------------
 * Remote Monitoring MIB Support
 *
 * August 1995, SNMP Research
 *
 * Copyright (c) 1995-1996 by cisco Systems, Inc.
 * All rights reserved.
 *------------------------------------------------------------------
 * $Log: sr_k_hist.c,v $
 * Revision 3.4.12.2  1996/07/01  23:48:15  snyder
 * CSCdi61839:  remove plural support from rmon
 * Branch: California_branch
 *              changes computed pluralizing of seconds to fixed second(s)
 *              48 bytes saved
 *
 * Revision 3.4.12.1  1996/05/17  21:26:29  anke
 * CSCdi48524:  Need mechanism to display RMON tables
 *              Fill in guts of show rmon commands
 * Branch: California_branch
 *
 * Revision 3.4  1996/02/19  06:06:01  jjohnson
 * CSCdi48853:  rmon can consume all available memory
 * As an interim measure, don't allow rmon to consume memory once the
 * heap's low-water mark has been reached.  While we're at it, name
 * all the malloc'ed memory so that we can tell what is used where
 *
 * Revision 3.3  1996/02/09  07:57:27  jjohnson
 * CSCdi48524:  Need mechanism to display RMON tables
 * parse chains are in place.  checkpoint work so that anke can take over.
 *
 * Revision 3.2  1995/11/17  18:39:28  hampton
 * Remove old entries from the RCS header logs.
 *
 * Revision 3.1  1995/11/09  13:03:17  shaker
 * Bump version numbers from 2.x to 3.x.
 *
 *------------------------------------------------------------------
 * $Endlog$
 */

/*
 *
 * Copyright (C) 1994 by SNMP Research, Incorporated.
 *
 * This software is furnished under a license and may be used and copied
 * only in accordance with the terms of such license and with the
 * inclusion of the above copyright notice. This software or any other
 * copies thereof may not be provided or otherwise made available to any
 * other person. No title to and ownership of the software is hereby
 * transferred.
 *
 * The information in this software is subject to change without notice
 * and should not be construed as a commitment by SNMP Research, Incorporated.
 *
 * Restricted Rights Legend:
 *  Use, duplication, or disclosure by the Government is subject to
 *  restrictions as set forth in subparagraph (c)(1)(ii) of the Rights
 *  in Technical Data and Computer Software clause at DFARS 52.227-7013
 *  and in similar clauses in the FAR and NASA FAR Supplement.
 *
 */

/*
 *                PROPRIETARY NOTICE
 *
 * This software is an unpublished work subject to a confidentiality agreement
 * and is protected by copyright and trade secret law.  Unauthorized copying,
 * redistribution or other use of this work is prohibited.
 *
 * The above notice of copyright on this source code product does not indicate
 * any actual or intended publication of such source code.
 *
 */


#include "master.h"
#include "sr_rmonmib.h"
#include "rmon_util.h"
#include "logger.h"
#include "../os/free.h"

/* global data describing the historyControlTable entries */
static SnmpType        historyControlEntryTypeTable[] = {
    {INTEGER_TYPE, SR_READ_ONLY, 0},	/* historyControlIndex */
    {OBJECT_ID_TYPE, SR_READ_WRITE, -1},	/* historyControlDataSource */
    {INTEGER_TYPE, SR_READ_WRITE, -1},	/* historyControlBucketsRequested */
    {INTEGER_TYPE, SR_READ_ONLY, -1},	/* historyControlBucketsGranted */
    {INTEGER_TYPE, SR_READ_WRITE, -1},	/* historyControlInterval */
    {OCTET_PRIM_TYPE, SR_READ_WRITE, -1},	/* historyControlOwner */
    {INTEGER_TYPE, SR_READ_WRITE, -1},	/* historyControlStatus */
};

static Index_t         historyControlEntryIndex[1];
static RmonTable       historyControlTable;

void DeletehistoryControlTableEntry SR_PROTOTYPE((int index));

/* this routine is not generated by the mib compiler.  It is responsible for
 * updating the statistics for appropriate control table entries. */
void
UpdateHistoryStatistics(psp)
    PacketState    *psp;
{
    int             i;
    historyControlEntry_t *hcp;
    etherHistoryEntry_t *hep;

    /* look for historyControl table entries with this data source */
    for (i = 0; i < historyControlTable.nitems; i++) {
	hcp = ((historyControlEntry_t **) historyControlTable.tp)[i];
	if (hcp->historyControlStatus == D_historyControlStatus_valid
	    && CmpDataSource(hcp->historyControlDataSource, psp->source) == 0
	    && hcp->current != 0) {
	    /* get a pointer to the current bucket */
	    hep = hcp->current;

	    /* update non-packet stats */
	    hep->etherHistoryCollisions += psp->collisions;
	    hep->etherHistoryDropEvents += psp->drops;
	    if (psp->size == 0)
		continue;

	    /* update statistics */
	    if (psp->error != 0) {
		/* crc or alignment error */
		if (psp->size < 64)
		    hep->etherHistoryFragments++;
		else if (psp->size > 1518)
		    hep->etherHistoryJabbers++;
		else
		    hep->etherHistoryCRCAlignErrors++;
	    }
	    else {
		/* no errors in the packet */
		if (psp->size < 64)
		    hep->etherHistoryUndersizePkts++;
		else if (psp->size > 1518)
		    hep->etherHistoryOversizePkts++;
		else {
		    /* check for destination */
		    if (IsBroadcast(psp->p))
			hep->etherHistoryBroadcastPkts++;
		    else if (*psp->p & 0x01)
			hep->etherHistoryMulticastPkts++;
		}
	    }

	    /* update all-packet statistics */
	    hep->etherHistoryPkts++;
	    hep->etherHistoryOctets += psp->size;
	}
    }
}

/* this routine advances the currently used bucket.  Buckets are maintained
 * as a doubly linked list.  */
void
NextHistoryBucketCallback(tdp)
    TimeOutDescriptor *tdp;
{
    historyControlEntry_t *hcp;
    etherHistoryEntry_t *cur, *new;
    long            etherHistorySampleIndex;

    /* get a pointer to the control entry */
    hcp = (historyControlEntry_t *) tdp->UserData2;

    /*
     * take the current bucket, set its utilization, and put it in the used
     * list
     */
    cur = hcp->current;
    if (cur != 0) {
	/*
	 * set the utilization.  In the formula, (cur->etherHistoryOctets +
	 * (cur->etherHistoryPkts * 8)) * 8) provides the number of bits on
	 * the wire during the interval, including framing bits.  Dividing by
	 * hcp->historyControlInterval gives the number of bits per second,
	 * and 1000 is the result of 10,000,000 / (100 * 100), or 1/100 of 1
	 * percent of the 10 Mbit/second bandwidth.
	 */
	cur->etherHistoryUtilization =
	    ((cur->etherHistoryOctets + (cur->etherHistoryPkts * 8)) * 8) /
	    (1000 * hcp->historyControlInterval);

	/* if the list was empty start it */
	if (hcp->newest == 0) {
	    hcp->newest = cur;
	    hcp->oldest = cur;
	}
	else {
	    /* add the current entry to the list */
	    hcp->newest->prev = cur;
	    cur->next = hcp->newest;
	    hcp->newest = cur;
	}
	hcp->newest->prev = 0;
	hcp->oldest->next = 0;
    }

    /* advance the bucket pointer */
    if (hcp->freelist != 0) {
	/* use the free list for the new pointer */
	new = hcp->freelist;
	hcp->freelist = new->next;
    }
    else {
	/* get the new one from the start of the used list */
	new = hcp->oldest;
	hcp->oldest = new->prev;

	/* if this empties the list, clear the newest pointer */
	if (hcp->oldest == 0) {
	    hcp->newest = 0;
	}
	else {
	    hcp->oldest->next = 0;
	}
    }

    /* clear the new bucket and initialize it */
    if (cur == 0) {
	/* this is the first sample */
	etherHistorySampleIndex = 0;
    }
    else {
	/* increment the old sample counter */
	etherHistorySampleIndex = cur->etherHistorySampleIndex;
    }
    memset(new, 0, sizeof(etherHistoryEntry_t));
    new->etherHistoryIndex = hcp->historyControlIndex;
    new->etherHistorySampleIndex = etherHistorySampleIndex + 1;
    new->etherHistoryIntervalStart = TimerCurTime();

    /* install this as the bucket to be used */
    hcp->current = new;
}

/* this routine attempts to allocate an array of buckets.  It returns a pointer
 * to the new table, and passes back the actual number of buckets by side
 * effect */
void
GetHistoryBuckets(hcp)
    historyControlEntry_t *hcp;
{
    long            i, count;
    etherHistoryEntry_t *new, *hep;

    /* determine whether to add or subtract buckets */
    count = hcp->historyControlBucketsRequested -
	hcp->historyControlBucketsGranted;
    if (count > 0) {
	/* don't proceed any further if we're low on memory */
	if (HeapLowMem())
	    return;

	/* start allocating entries */
	for (i = 0; i < count; i++) {
	    new = malloc_named(sizeof(etherHistoryEntry_t),
			       "RMON Hist Data");
	    if (new == 0) {
		break;
	    }

	    /* add it to the list */
	    new->next = hcp->freelist;
	    hcp->freelist = new;

	    /* update the granted count */
	    hcp->historyControlBucketsGranted++;
	}
    }
    else if (count < 0) {
	/* convert to a positive count */
	count = 0 - count;

	/* free entries from the free list, if possible */
	i = 0;
	while (hcp->freelist != 0 && i < count) {
	    hep = hcp->freelist;
	    hcp->freelist = hep->next;
	    free(hep);
	    i++;
	}

	/* free them oldest first, otherwise */
	while (hcp->oldest != 0 && i < count) {
	    hep = hcp->oldest;
	    hcp->oldest = hep->prev;
	    if (hcp->oldest == 0) {
		hcp->newest = 0;
	    }
	    else {
		hcp->oldest->next = 0;
	    }
	    free(hep);
	    i++;
	}

	/* finally, free the current one */
	if (i != count) {
	    free(hcp->current);
	    hcp->current = 0;
	    i++;
	}

	/* make sure we freed them all */
	if (i != count) {
	    DPRINTF((APERROR, "GetHistoryBuckets: couldn't free %ld buckets\n",
		     count));
	}

	/* adjust the granted count */
	hcp->historyControlBucketsGranted -= count;
    }
}

/* this routine installs a timer for the history system */
void           *
SetHistoryBucketTimer(hcp)
    historyControlEntry_t *hcp;
{
    long            delay, thour, when;
    TimeOutDescriptor *tdp;

    tdp = malloc_named(sizeof(TimeOutDescriptor), "RMON Hist Timer");
    if (tdp) {
	/*
	 * set the callback timer.  It should go off at the start of the next
	 * hour, if possible
	 */
	thour = 3600 - ((CurTimeOfDay() / 100) % 3600);
	delay = thour - ((thour / hcp->historyControlInterval) *
			 hcp->historyControlInterval);
	when = (TimerCurTime() / 100) + delay;

	/* fill in the descriptor */
	tdp->CallBack = NextHistoryBucketCallback;
	tdp->UserData2 = (void *) hcp;

	/* set the timer */
	if ((tdp->TimerId = SetPeriodicTimeout(when * 100,
			  (unsigned long) hcp->historyControlInterval * 100,
					       (void *) tdp)) == (-1)) {
	    /* free the descriptor */
	    free(tdp);
	    tdp = 0;
	}
    }

    /* return the timeout descriptor */
    return ((void *) tdp);
}

/* --------------------- MIB COMPILER GENERATED ------------------------- */

int
k_historyControlEntry_initialize()
{
    historyControlEntryIndex[0].offset = I_historyControlIndex;
    historyControlEntryIndex[0].type = T_uint;

    historyControlTable.nindices = 1;
    historyControlTable.tip = historyControlEntryIndex;
    historyControlTable.nitems = 0;
    historyControlTable.rowsize = sizeof(historyControlEntry_t);
    if ((historyControlTable.tp = (void **) malloc(sizeof(historyControlEntry_t *))) == NULL) {
	return 0;
    }

    return 1;
}

int
k_historyControlEntry_terminate()
{
    while (historyControlTable.nitems > 0) {
	DeletehistoryControlTableEntry(0);
    }

    free(historyControlTable.tp);
    return 1;
}

/* This routine is called by the timeout code to
 * delete a pending creation of a Table entry */
void
historyControlTableDeleteCallback(tdp)
    TimeOutDescriptor *tdp;
{
    historyControlEntry_t *data;

    /* dummy up an entry to delete */
    data = (historyControlEntry_t *) tdp->UserData2;
    data->historyControlStatus = D_historyControlStatus_invalid;
    data->TimerId = -1;

    /* free the timeout descriptor */
    free(tdp);

    /* call the set method */
    k_historyControlEntry_set(data, (ContextInfo *) NULL, 0);
}

/* This routine deletes an entry from the historyControlTable */
void
DeletehistoryControlTableEntry(index)
    int             index;
{
    historyControlEntry_t *data;
    TimeOutDescriptor *tdp;

    /* get a pointer to the old entry */
    data = (historyControlEntry_t *) historyControlTable.tp[index];

    /* free the periodic timer associated with the entry */
    if (data->NextBucketTimer != 0) {
	tdp = (TimeOutDescriptor *) data->NextBucketTimer;
	if (tdp->TimerId != -1) {
	    /* cancel the timer */
	    CancelTimeout((int) tdp->TimerId);
	}
	free(tdp);
	data->NextBucketTimer = 0;
    }

    /* free the old entry and remove it from the table */
    historyControlEntry_free(data);
    RemoveTableEntry(&historyControlTable, index);
}

/* This routine can be used to free data which
 * is defined in the userpart part of the structure */
void
k_historyControlEntryFreeUserpartData(data)
    historyControlEntry_t *data;
{
    /* nothing to free by default */
}

historyControlEntry_t *
k_historyControlEntry_get(serialNum, contextInfo, nominator, searchType, historyControlIndex)
    int             serialNum;
    ContextInfo    *contextInfo;
    int             nominator;
    int             searchType;
    long            historyControlIndex;
{
    int             index;

    historyControlTable.tip[0].value.uint_val = historyControlIndex;
    if ((index = SearchTable(&historyControlTable, searchType)) == -1) {
	return NULL;
    }

    return (historyControlEntry_t *) historyControlTable.tp[index];

}

#ifdef SETS
int
k_historyControlEntry_test(object, value, dp, contextInfo)
    ObjectInfo     *object;
    ObjectSyntax   *value;
    doList_t       *dp;
    ContextInfo    *contextInfo;
{

#ifdef SR_SNMPv2
    historyControlEntry_t *undodata = (historyControlEntry_t *) dp->undodata;
#else				/* SR_SNMPv2 */
    historyControlEntry_t *undodata = NULL;
#endif				/* SR_SNMPv2 */
    int             error_status = NO_ERROR;

    /*
     * make sure that certain entries aren't being changed after the control
     * entry is set to valid
     */
    if (undodata != 0
	&& undodata->historyControlStatus == D_historyControlStatus_valid) {
	/* this is a valid entry */
	switch (object->nominator) {
	  case I_historyControlDataSource:
	    if (CmpOID(value->oid_value, undodata->historyControlDataSource)
		!= 0) {
		error_status = INCONSISTENT_VALUE_ERROR;
	    }
	    break;
	  case I_historyControlInterval:
	    if (value->sl_value != undodata->historyControlInterval) {
		error_status = INCONSISTENT_VALUE_ERROR;
	    }
	    break;
	}
    }

    return (error_status);
}

int
k_historyControlEntry_ready(object, value, doHead, dp)
    ObjectInfo     *object;
    ObjectSyntax   *value;
    doList_t       *doHead;
    doList_t       *dp;
{

    historyControlEntry_t *data = (historyControlEntry_t *) dp->data;

    if (data->historyControlStatus == D_historyControlStatus_invalid) {
	dp->state = DELETE;
    }
    else {
	dp->state = ADD_MODIFY;

	/* if setting to valid, make sure we have all the information we need */
	if (data->historyControlStatus == D_historyControlStatus_valid) {
	    if (!VALID(I_historyControlStatus, data->valid)
		|| CheckEthernetDataSource(data->historyControlDataSource) == 0) {
		/* can't do it, don't have a valid data source */
		dp->state = UNKNOWN;
	    }
	}
    }

    return NO_ERROR;
}

int
k_historyControlEntry_set_defaults(dp)
    doList_t       *dp;
{

    historyControlEntry_t *data = (historyControlEntry_t *) dp->data;

    /*
     * if we're low on memory, don't allow the entry to be created
     */
    if (HeapLowMem())
	return RESOURCE_UNAVAILABLE_ERROR;

    data->historyControlBucketsRequested = 50;
    data->historyControlInterval = 1800;

    if ((data->historyControlDataSource = MakeOIDFromDot("0.0")) == 0) {
	return RESOURCE_UNAVAILABLE_ERROR;
    }

    if ((data->historyControlOwner = MakeOctetStringFromText("")) == 0) {
	return RESOURCE_UNAVAILABLE_ERROR;
    }

    data->historyControlStatus = D_historyControlStatus_createRequest;
    data->TimerId = -1;
    data->NextBucketTimer = 0;

    SET_ALL_VALID(data->valid);

    return NO_ERROR;
}

int
k_historyControlEntry_set(data, contextInfo, function)
    historyControlEntry_t *data;
    ContextInfo    *contextInfo;
    int             function;
{

    int             index;
    historyControlEntry_t *new = NULL;

    /* find this entry in the table */
    historyControlTable.tip[0].value.uint_val = data->historyControlIndex;
    if ((index = SearchTable(&historyControlTable, EXACT)) != -1) {
	new = (historyControlEntry_t *) historyControlTable.tp[index];
    }

    /* perform the table entry operation on it */
    if (data->historyControlStatus == D_historyControlStatus_invalid) {
	if (data->TimerId != -1) {
	    CancelEntryStatusTimeout((int) data->TimerId);
	}

	if (index == -1) {
	    return NO_ERROR;
	}
	else {
	    /* delete all allocated buckets */
	    new->historyControlBucketsRequested = 0;
	    GetHistoryBuckets(new);

	    /* delete the table entry */
	    DeletehistoryControlTableEntry(index);
	    return NO_ERROR;
	}
    }
    else if (index == -1) {
	/* add the entry */
	historyControlTable.tip[0].value.uint_val = data->historyControlIndex;
	if ((index = NewTableEntry(&historyControlTable)) == -1) {
	    return GEN_ERROR;
	}
	new = (historyControlEntry_t *) historyControlTable.tp[index];
	name_memory(new, "RMON Hist Ctrl");

	/* set a timeout */
	if ((new->TimerId = SetEntryStatusTimeout(MAX_ROW_CREATION_TIME, (void *) new, historyControlTableDeleteCallback)) == -1) {
	    DeletehistoryControlTableEntry(index);
	    return GEN_ERROR;
	}
    }

    /* transfer mib data */
    TransferEntries(I_historyControlStatus,
		    historyControlEntryTypeTable,
		    (unsigned long *)new, (unsigned long *)data);
    SET_ALL_VALID(new->valid);

    /* transfer private data */
    new->NextBucketTimer = data->NextBucketTimer;
    new->oldest = data->oldest;
    new->newest = data->newest;
    new->current = data->current;
    new->freelist = data->freelist;

    /* actually try getting buckets, if necessary */
    GetHistoryBuckets(new);

    /* if the new row entry is now valid, cancel the creation timeout */
    if (new->historyControlStatus == D_historyControlStatus_valid && new->TimerId != -1) {
	CancelEntryStatusTimeout((int) new->TimerId);
	new->TimerId = -1;

	/* if we can't get the timer, cancel the table creation */
	if ((new->NextBucketTimer = SetHistoryBucketTimer(new)) == 0) {
	    DeletehistoryControlTableEntry(index);
	    return (GEN_ERROR);
	}
    }
    else {
	if (new->historyControlStatus == D_historyControlStatus_createRequest) {
	    new->historyControlStatus = D_historyControlStatus_underCreation;
	}
    }

    return NO_ERROR;
}

#endif				/* SETS */

#ifdef SR_SNMPv2
int
historyControlEntry_undo(doHead, doCur, contextInfo)
    doList_t       *doHead;
    doList_t       *doCur;
    ContextInfo    *contextInfo;
{
    /* can't undo from this operation */
    return UNDO_FAILED_ERROR;
}

#endif				/* SR_SNMPv2 */

etherHistoryEntry_t *
k_etherHistoryEntry_get(serialNum, contextInfo, nominator, searchType, etherHistoryIndex, etherHistorySampleIndex)
    int             serialNum;
    ContextInfo    *contextInfo;
    int             nominator;
    int             searchType;
    long            etherHistoryIndex;
    long            etherHistorySampleIndex;
{
    historyControlEntry_t *hcp;
    etherHistoryEntry_t *hep, *prevhep;
    etherHistoryEntry_t *data;
    int             index;

    /* repeat until we find an appropriate match, or there is no more data */
    data = NULL;
    do {
	/* get the history control entry */
	historyControlTable.tip[0].value.uint_val = etherHistoryIndex;
	if ((index = SearchTable(&historyControlTable, searchType)) == -1) {
	    /* no such control table */
	    break;
	}
	hcp = (historyControlEntry_t *) historyControlTable.tp[index];

	/* search the history, newest first, for the entry */
	prevhep = 0;
	for (hep = hcp->newest; hep != 0; hep = hep->next) {
	    if (hep->etherHistorySampleIndex <= etherHistorySampleIndex) {
		break;
	    }
	    prevhep = hep;
	}

	/* check for an exact match */
	if (hep != 0) {
	    if (hep->etherHistorySampleIndex == etherHistorySampleIndex) {
		data = hep;
	    }
	}

	/* did we find it? */
	if (data == NULL && searchType == NEXT) {
	    if (prevhep != 0) {
		/* the preceding entry was the next larger */
		data = prevhep;
	    }
	    else {
		/* no entries, look in the next table */
		etherHistoryIndex++;
		etherHistorySampleIndex = 0;
	    }
	}
    } while (data == NULL && searchType == NEXT);

    /* return the data */
    return (data);
}

/******************** cisco supplied routines follow ********************/

/*
 * show rmon history
 * output a string similiar to the following for every history entry:
 *    Entry 12345 is valid, and owned by foo
 *      Monitors oid.oid.oid.oid.oid.oid every xxxx seconds
 *      Requested # of time intervals, ie buckets, is xxxxxx
 *      Granted # of time intervals, ie buckets, is xxxxxx
 *    Sample # xxxxxx began measuring at xxxxxx
 *      Received xxxxxx octets, xxxxxx packets,
 *      xxxxx broadcast and xxxxx multicast packets,
 *      xxxxx undersized and xxxxxx oversized packets,
 *      xxxxx fragments and xxxxxx jabbers,
 *      xxxxxx CRC alignment errors and xxxxxx collisions.
 *      # of dropped packet events is xxxxxxxx
 *      Network utilization is estimated at xxxxxx  
 */

void
show_rmon_history (void)
{
    historyControlEntry_t   *historyp;
    etherHistoryEntry_t     *hep;
    int             i, status;
    OctetString    *ownerOS;
    char	   *owner;
    char           *object;

    automore_enable(NULL);
    if (historyControlTable.nitems == 0) {
	printf("\nHistory Control table is empty");
	automore_disable();
	return;
    }

    for (i = 0; i < historyControlTable.nitems; i++) {
        historyp = historyControlTable.tp[i];
	mem_lock(historyp);

	/* turn the owner string from an octet string to a real string */
	owner = malloc(historyp->historyControlOwner->length + 1);
	if (owner) {
	    ownerOS = historyp->historyControlOwner;
	    memcpy(owner, ownerOS->octet_ptr, ownerOS->length);
	}
	else
	    return;

	/* get a textual representation of the object */
	object = malloc(MAX_OCTET_STRING_SIZE);
	if (object) {
	    status =
		MakeDotFromOID(historyp->historyControlDataSource, object);
	    if (status == -1) {
		free(object);
		object = NULL;
	    }
	}

        printf("\nEntry %d is %s, and owned by %s",
	       historyp->historyControlIndex,
	       rmon_status_string(historyp->historyControlStatus),
               owner ? owner : "unknown");
        printf("\n Monitors %s every %d second(s)", 
               object ? object : "unknown",
	       historyp->historyControlInterval); 
        printf("\n Requested # of time intervals, ie buckets, is %d",
	       historyp->historyControlBucketsRequested); 
        printf("\n Granted # of time intervals, ie buckets, is %d",
               historyp->historyControlBucketsGranted);
	free(owner);
        free(object);

        for (hep = historyp->oldest; hep != NULL; hep = hep->prev) {
	    printf("\n  Sample # %lu began measuring at %TC",
		   hep->etherHistorySampleIndex,
		   hep->etherHistoryIntervalStart); 
	    printf("\n   Received %lu octets, %lu packets,",
		   hep->etherHistoryOctets,
		   hep->etherHistoryPkts);
	    printf("\n   %lu broadcast and %lu multicast packets,",
		   hep->etherHistoryBroadcastPkts,
		   hep->etherHistoryMulticastPkts); 
	    printf("\n   %lu undersized and %lu oversized packets,",
		   hep->etherHistoryUndersizePkts,
		   hep->etherHistoryOversizePkts); 
	    printf("\n   %lu fragments and %lu jabbers,",
		   hep->etherHistoryFragments,
		   hep->etherHistoryJabbers); 
	    printf("\n   %lu CRC alignment errors and %lu collisions.",
		   hep->etherHistoryCRCAlignErrors,
		   hep->etherHistoryCollisions);
	    printf("\n   # of dropped packet events is %lu",
                   hep->etherHistoryDropEvents);
	    printf("\n   Network utilization is estimated at %l",
		   hep->etherHistoryUtilization);
	}
	free(historyp);
    }
    automore_disable();
}

