//=====================================================================
//
// imemkind.h - utilities for imemdata.c and imembase.c
//
// NOTE:
// For more information, please see the readme file.
//
//=====================================================================
#ifndef _IMEMKIND_H_
#define _IMEMKIND_H_

#include <stddef.h>

#include "imembase.h"
#include "imemdata.h"

#ifdef __cplusplus
extern "C" {
#endif


//---------------------------------------------------------------------
// Utilities
//---------------------------------------------------------------------

// push message into stream
void iposix_msg_push(struct IMSTREAM *queue, IINT32 msg, IINT32 wparam,
		IINT32 lparam, const void *data, IINT32 size);


// read message from stream
IINT32 iposix_msg_read(struct IMSTREAM *queue, IINT32 *msg, 
		IINT32 *wparam, IINT32 *lparam, void *data, IINT32 maxsize);


#ifdef __cplusplus
}
#endif


#endif



