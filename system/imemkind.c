//=====================================================================
//
// imemkind.h - utilities for imemdata.c and imembase.c
//
// NOTE:
// For more information, please see the readme file.
//
//=====================================================================
#include <stddef.h>

#include "imemkind.h"


//=====================================================================
// Utilities
//=====================================================================

// push message into stream
void iposix_msg_push(struct IMSTREAM *queue, IINT32 msg, IINT32 wparam,
		IINT32 lparam, const void *data, IINT32 size)
{
	char head[16];
	iencode32u_lsb(head + 0, 16 + size);
	iencode32i_lsb(head + 4, msg);
	iencode32i_lsb(head + 8, wparam);
	iencode32i_lsb(head + 12, lparam);
	ims_write(queue, head, 16);
	ims_write(queue, data, size);
}


// read message from stream
IINT32 iposix_msg_read(struct IMSTREAM *queue, IINT32 *msg, 
		IINT32 *wparam, IINT32 *lparam, void *data, IINT32 maxsize)
{
	IINT32 length, size, cc;
	char head[16];
	if (queue->size < 16) return -1;
	ims_peek(queue, &length, 4);
	assert(length >= 16);
	size = length - 16;
	if ((IINT32)(queue->size) < length) return -1;
	if (data == NULL) return size;
	if (maxsize < (int)size) return -2;
	ims_read(queue, head, 16);
	idecode32i_lsb(head + 4, msg);
	idecode32i_lsb(head + 8, wparam);
	idecode32i_lsb(head + 12, lparam);
	cc = (IINT32)ims_read(queue, data, size);
	assert(cc == size);
	return size;
}



