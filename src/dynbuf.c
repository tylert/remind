/***************************************************************/
/*                                                             */
/*  DYNBUF.C                                                   */
/*                                                             */
/*  Implementation of functions for manipulating dynamic       */
/*  buffers.                                                   */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-1998 by David F. Skoll                  */
/*  Copyright (C) 1999 by Roaring Penguin Software Inc.        */
/*                                                             */
/***************************************************************/

static char const RCSID[] =
"$Id: dynbuf.c,v 1.3 1999-04-05 17:34:45 dfs Exp $";

#include "config.h"
#include "dynbuf.h"
#include "err.h"
#include <string.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

/**********************************************************************
%FUNCTION: DBufMakeRoom
%ARGUMENTS:
 dbuf -- pointer to a dynamic buffer
 n -- size to expand to 
%RETURNS:
 OK if all went well; E_NO_MEM if out of memory
%DESCRIPTION:
 Doubles the size of dynamic buffer until it has room for at least
 'n' characters, not including trailing '\0'
**********************************************************************/
#ifdef HAVE_PROTOS
PRIVATE int DBufMakeRoom(DynamicBuffer *dbuf, int n)
#else
static int DBufMakeRoom(dbuf, n)
DynamicBuffer *dbuf;
int n;
#endif
{
    /* Double size until it's greater than n (strictly > to leave room
       for trailing '\0' */
    int size = dbuf->allocatedLen;
    char *buf;

    if (size > n) return OK;

    while (size <= n) {
	size *= 2;
    }

    /* Allocate memory */
    buf = (char *) malloc(size);
    if (!buf) return E_NO_MEM;

    /* Copy contents */
    strcpy(buf, dbuf->buffer);

    /* Free contents if necessary */
    if (dbuf->buffer != dbuf->staticBuf) free(dbuf->buffer);
    dbuf->buffer = buf;
    dbuf->allocatedLen = size;
    return OK;
}

/**********************************************************************
%FUNCTION: DBufInit
%ARGUMENTS:
 dbuf -- pointer to a dynamic buffer
%RETURNS:
 Nothing
%DESCRIPTION:
 Initializes a dynamic buffer
**********************************************************************/
#ifdef HAVE_PROTOS
PUBLIC void DBufInit(DynamicBuffer *dbuf)
#else
void DBufInit(dbuf)
DynamicBuffer *dbuf;
#endif
{
    dbuf->buffer = dbuf->staticBuf;
    dbuf->len = 0;
    dbuf->allocatedLen = DBUF_STATIC_SIZE;
    dbuf->buffer[0] = 0;
}

/**********************************************************************
%FUNCTION: DBufPutc
%ARGUMENTS:
 dbuf -- pointer to a dynamic buffer
 c -- character to append to buffer
%RETURNS:
 OK if all went well; E_NO_MEM if out of memory
%DESCRIPTION:
 Appends a character to the buffer.
**********************************************************************/
#ifdef HAVE_PROTOS
PUBLIC int DBufPutc(DynamicBuffer *dbuf, char c)
#else
int DBufPutc(dbuf, c)
DynamicBuffer *dbuf;
char c;
#endif
{
    if (dbuf->allocatedLen == dbuf->len+1) {
	if (DBufMakeRoom(dbuf, dbuf->len+1) != OK) return E_NO_MEM;
    }
    dbuf->buffer[dbuf->len++] = c;
    dbuf->buffer[dbuf->len] = 0;
    return OK;
}

/**********************************************************************
%FUNCTION: DBufPuts
%ARGUMENTS:
 dbuf -- pointer to a dynamic buffer
 str -- string to append to buffer
%RETURNS:
 OK if all went well; E_NO_MEM if out of memory
%DESCRIPTION:
 Appends a string to the buffer.
**********************************************************************/
#ifdef HAVE_PROTOS
PUBLIC int DBufPuts(DynamicBuffer *dbuf, char *str)
#else
int DBufPuts(dbuf, str)
DynamicBuffer *dbuf;
char *str;
#endif
{
    int l = strlen(str);
    if (!l) return OK;

    if (DBufMakeRoom(dbuf, dbuf->len+l) != OK) return E_NO_MEM;
    strcpy((dbuf->buffer+dbuf->len), str);
    dbuf->len += l;
    return OK;
}

/**********************************************************************
%FUNCTION: DBufFree
%ARGUMENTS:
 dbuf -- pointer to a dynamic buffer
%RETURNS:
 Nothing
%DESCRIPTION:
 Frees and reinitializes a dynamic buffer
**********************************************************************/
#ifdef HAVE_PROTOS
PUBLIC void DBufFree(DynamicBuffer *dbuf)
#else
void DBufFree(dbuf)
DynamicBuffer *dbuf;
#endif
{
    if (dbuf->buffer != dbuf->staticBuf) free(dbuf->buffer);
    DBufInit(dbuf);
}

/**********************************************************************
%FUNCTION: DBufGets
%ARGUMENTS:
 dbuf -- pointer to a dynamic buffer
 fp -- file to read from
%RETURNS:
 OK or E_NO_MEM
%DESCRIPTION:
 Reads an entire line from a file and appends to dbuf.  Does not include
 trailing newline.
**********************************************************************/
#ifdef HAVE_PROTOS
PUBLIC int DBufGets(DynamicBuffer *dbuf, FILE *fp)
#else
int DBufGets(dbuf, fp)
DynamicBuffer *dbuf;
FILE *fp
#endif
{
    char tmp[256]; /* Safe to hard-code */
    int busy = 1;
    int l;

    DBufFree(dbuf);

    /* Try reading the first few bytes right into the buffer --
       we can usually save some unnecessary copying */

    *(dbuf->buffer) = 0;
    fgets(dbuf->buffer, dbuf->allocatedLen, fp);
    if (!*(dbuf->buffer)) return OK;
    dbuf->len = strlen(dbuf->buffer);
    l = dbuf->len - 1;
    if (dbuf->buffer[l] == '\n') {
	dbuf->buffer[l] = 0;
	dbuf->len = l;
	return OK;
    }

    while(busy) {
	*tmp = 0;
	fgets(tmp, 256, fp);
	if (!*tmp) return OK;
	l = strlen(tmp) - 1;
	if (tmp[l] == '\n') {
	    tmp[l] = 0;
	    busy = 0;
	}
	if (DBufPuts(dbuf, tmp) != OK) return E_NO_MEM;
    }
    return OK;
}

