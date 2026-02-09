/**********************************************************************
 *
 * imemdata.c - basic data structures and algorithms
 * skywind3000 (at) gmail.com, 2006-2016
 *
 * ISTREAM - definition of the basic stream interface
 *
 * VALUE OPERATION
 * type independance value classes
 *
 * DICTIONARY OPERATION
 *
 * feature: 2.1-2.2 times faster than std::map (string or integer key)
 * feature: 1.3-1.5 times faster than stdext::hash_map 
 *
 * for more information, please see the readme file
 *
 **********************************************************************/
#include "imemdata.h"

#include <ctype.h>
#include <assert.h>


/*====================================================================*/
/* IRING: The struct definition of the ring buffer                    */
/*====================================================================*/


/* init circle cache */
void iring_init(struct IRING *ring, void *buffer, ilong capacity)
{
	ring->data = (char*)buffer;
	ring->capacity = capacity;
	ring->head = 0;
}

/* return head position */
ilong iring_head(const struct IRING *ring)
{
	assert(ring);
	return ring->head;
}

/* calculate new position within the range of [0, capacity) */
ilong iring_modulo(const struct IRING *ring, ilong offset)
{
	ilong cap = ring->capacity;
	if (cap == 0) {
		return 0;
	}
	if (offset >= 0) {
		if (offset >= cap) {
			offset -= cap;
			offset = (offset < cap)? offset : (offset % cap);
			if (offset >= cap) 
				offset %= cap;
		}
		return offset;
	}
	else {
		offset = -offset;
		if (offset >= cap) {
			offset %= cap;
		}
		return (offset > 0) ? cap - offset : 0;
	}
}

/* move head forward */
ilong iring_advance(struct IRING *ring, ilong offset)
{
	ilong cap = ring->capacity;
	if (cap <= 0) {
		return ring->head;
	}
	ring->head = iring_modulo(ring, ring->head + offset);
	return ring->head;
}

/* fetch data from position */
ilong iring_read(const struct IRING *ring, ilong pos, void *ptr, ilong len)
{
	char *lptr = (char*)ptr;
	ilong cap = ring->capacity;
	ilong offset = iring_modulo(ring, ring->head + pos);
	ilong half = cap - offset;
	if (cap <= 0) {
		return 0;
	}
	len = (len < cap)? len : cap;
	if (half >= len) {
		memcpy(lptr, ring->data + offset, (size_t)len);
	}	else {
		memcpy(lptr, ring->data + offset, (size_t)half);
		memcpy(lptr + half, ring->data, (size_t)(len - half));
	}
	return len;
}

/* store data to position */
ilong iring_write(struct IRING *ring, ilong pos, const void *ptr, ilong len)
{
	const char *lptr = (const char*)ptr;
	ilong cap = ring->capacity;
	ilong offset = iring_modulo(ring, ring->head + pos);
	ilong half = cap - offset;
	if (cap <= 0) {
		return 0;
	}
	len = (len < cap)? len : cap;
	if (half >= len) {
		memcpy(ring->data + offset, lptr, (size_t)len);
	}	else {
		memcpy(ring->data + offset, lptr, (size_t)half);
		memcpy(ring->data, lptr + half, (size_t)(len - half));
	}
	return len;
}

/* fill data into position */
ilong iring_fill(struct IRING *ring, ilong pos, unsigned char ch, ilong len)
{
	ilong cap = ring->capacity;
	ilong offset = iring_modulo(ring, ring->head + pos);
	ilong half = cap - offset;
	if (cap <= 0) {
		return 0;
	}
	len = (len < cap)? len : cap;
	if (half >= len) {
		memset(ring->data + offset, ch, (size_t)len);
	}	else {
		memset(ring->data + offset, ch, (size_t)half);
		memset(ring->data, ch, (size_t)(len - half));
	}
	return len;
}

/* flat memory */
ilong iring_flat(const struct IRING *ring, void **pointer)
{
	if (pointer) pointer[0] = (void*)(ring->data + ring->head);
	return ring->capacity - ring->head;
}

/* swap internal buffer */
void iring_swap(struct IRING *ring, void *buffer, ilong capacity)
{
	ilong size = (ring->capacity < capacity)? ring->capacity : capacity;
	iring_read(ring, 0, buffer, size);
	ring->data = (char*)buffer;
	ring->capacity = capacity;
	ring->head = 0;
}

/* get two pointers and sizes */
void iring_ptrs(struct IRING *ring, void **p1, ilong *s1, 
	void **p2, ilong *s2)
{
	ilong half = ring->capacity - ring->head;
	p1[0] = ring->data + ring->head;
	s1[0] = half;
	p2[0] = ring->data;
	s2[0] = ring->head;
}



/*====================================================================*/
/* IMSTREAM: In-Memory FIFO Buffer                                    */
/*====================================================================*/
struct IMSPAGE
{
	struct ILISTHEAD head;
	iulong size;
	iulong index;
	IUINT8 data[2];
};

#define IMSPAGE_LRU_SIZE	2

/* init memory stream */
void ims_init(struct IMSTREAM *s, ib_memnode *fnode, ilong low, ilong high)
{
	ilong swap;
	ilist_init(&s->head);
	ilist_init(&s->lru);
	s->fixed_pages = fnode;
	s->pos_read = 0;
	s->pos_write = 0;
	s->size = 0;
	low = low < 1024 ? 1024 : (low >= 0x10000 ? 0x10000 : low);
	high = high < 1024 ? 1024 : (high >= 0x10000 ? 0x10000 : high);
	if (low >= high) {
		swap = low;
		low = high;
		high = swap;
	}
	s->hiwater = high;
	s->lowater = low;
	s->lrusize = 0;
}

/* alloc new page from kmem-system or IMEMNODE */
static struct IMSPAGE *ims_page_new(struct IMSTREAM *s)
{
	struct IMSPAGE *page;
	ilong newsize, index;

	newsize = sizeof(struct IMSPAGE) + s->size;
	newsize = newsize >= s->hiwater ? s->hiwater : newsize;
	newsize = newsize <= s->lowater ? s->lowater : newsize;

	if (s->fixed_pages != NULL) {
		index = imnode_new(s->fixed_pages);
		if (index < 0) return NULL;
		page = (struct IMSPAGE*)IMNODE_DATA(s->fixed_pages, index);
		page->index = (iulong)index;
		page->size = s->fixed_pages->node_size;
		page->size = page->size - sizeof(struct IMSPAGE);
	}	else {
		page = (struct IMSPAGE*)ikmem_malloc(newsize);
		if (page == NULL) return NULL;
		page->index = (iulong)0xfffffffful;
		page->size = newsize - sizeof(struct IMSPAGE);
	}

	ilist_init(&page->head);

	return page;
}

/* free page into kmem-system or IMEMNODE */
static void ims_page_del(struct IMSTREAM *s, struct IMSPAGE *page)
{
	if (s->fixed_pages != NULL) {
		assert(page->index != (iulong)0xfffffffful);
		imnode_del(s->fixed_pages, page->index);
	}	else {
		assert(page->index == (iulong)0xfffffffful);
		ikmem_free(page);
	}
}

/* destroy memory stream */
void ims_destroy(struct IMSTREAM *s)
{
	struct IMSPAGE *current;
	assert(s);

	for (; ilist_is_empty(&s->head) == 0; ) {
		current = ilist_entry(s->head.next, struct IMSPAGE, head);
		ilist_del(&current->head);
		ims_page_del(s, current);
	}

	for (; ilist_is_empty(&s->lru) == 0; ) {
		current = ilist_entry(s->lru.next, struct IMSPAGE, head);
		ilist_del(&current->head);
		ims_page_del(s, current);
	}

	s->pos_read = 0;
	s->pos_write = 0;
	s->size = 0;
	s->lrusize = 0;
}

/* get page from lru cache */
static struct IMSPAGE *ims_page_cache_get(struct IMSTREAM *s)
{
	struct IMSPAGE *page;
	ilong i;
	if (s->lrusize == 0) {
		for (i = 0; i < IMSPAGE_LRU_SIZE; i++) {
			page = ims_page_new(s);
			if (page == NULL) {
				ASSERTION(page);
				abort();
			}
			ilist_add_tail(&page->head, &s->lru);
			s->lrusize++;
		}
	}

	assert(s->lru.next != &s->lru);
	assert(s->lrusize > 0);

	page = ilist_entry(s->lru.next, struct IMSPAGE, head);
	ilist_del(&page->head);
	s->lrusize--;

	return page;
}

/* give page back to lru cache */
static void ims_page_cache_release(struct IMSTREAM *s, struct IMSPAGE *page)
{
	ilist_add_tail(&page->head, &s->lru);
	s->lrusize++;
	for (; s->lrusize > (IMSPAGE_LRU_SIZE << 1); ) {
		page = ilist_entry(s->lru.next, struct IMSPAGE, head);
		ilist_del(&page->head);
		s->lrusize--;
		ims_page_del(s, page);
	}
}

/* get data size */
ilong ims_dsize(const struct IMSTREAM *s)
{
	return s->size;
}

/* write data into memory stream */
ilong ims_write(struct IMSTREAM *s, const void *ptr, ilong size)
{
	ilong total, canwrite, towrite;
	struct IMSPAGE *current;
	const IUINT8 *lptr;

	assert(s);

	if (size <= 0) return size;
	lptr = (const IUINT8*)ptr;

	for (total = 0; size > 0; size -= towrite, total += towrite) {
		if (ilist_is_empty(&s->head)) {
			current = NULL;
			canwrite = 0;
		}	else {
			current = ilist_entry(s->head.prev, struct IMSPAGE, head);
			canwrite = current->size - s->pos_write;
		}
		if (canwrite == 0) {
			current = ims_page_cache_get(s);
			assert(current);
			ilist_add_tail(&current->head, &s->head);
			s->pos_write = 0;
			canwrite = current->size;
		}
		towrite = (size <= canwrite)? size : canwrite;
		if (lptr != NULL) {
			if (towrite > 0) {
				memcpy(current->data + s->pos_write, lptr, towrite);
			}
			lptr += towrite;
		}
		s->pos_write += towrite;
		s->size += towrite;
	}

	return total;
}

/* memory stream main read routine */
ilong ims_read_sub(struct IMSTREAM *s, void *ptr, ilong size, int nodrop)
{
	ilong total, canread, toread, posread;
	struct ILISTHEAD *head;
	struct IMSPAGE *current;
	IUINT8 *lptr;

	assert(s);

	if (size <= 0) return size;
	lptr = (IUINT8*)ptr;
	posread = s->pos_read;
	head = s->head.next;

	for (total = 0; size > 0; size -= toread, total += toread) {
		if (head == &s->head) break;
		current = ilist_entry(head, struct IMSPAGE, head);
		head = head->next;
		if (head == &s->head) canread = s->pos_write - posread;
		else canread = current->size - posread;
		toread = (size <= canread)? size : canread;
		if (toread == 0) break;
		if (lptr) {
			if (toread > 0) {
				memcpy(lptr, current->data + posread, toread);
			}
			lptr += toread;
		}
		posread += toread;
		if (posread >= (ilong)current->size) {
			posread = 0;
			if (nodrop == 0) {
				ilist_del(&current->head);
				ims_page_cache_release(s, current);
				if (ilist_is_empty(&s->head)) {
					s->pos_write = 0;
				}
			}
		}
		if (nodrop == 0) {
			s->size -= toread;
			s->pos_read = posread;
		}
	}
	return total;
}

/* read (and drop) data from memory stream */
ilong ims_read(struct IMSTREAM *s, void *ptr, ilong size)
{
	assert(s && ptr);
	return ims_read_sub(s, ptr, size, 0);
}

/* peek (no drop) data from memory stream */
ilong ims_peek(const struct IMSTREAM *s, void *ptr, ilong size)
{
	assert(s && ptr);
	return ims_read_sub((struct IMSTREAM*)s, ptr, size, 1);
}

/* drop data from memory stream */
ilong ims_drop(struct IMSTREAM *s, ilong size)
{
	assert(s);
	return ims_read_sub(s, NULL, size, 0);
}

/* clear stream */
void ims_clear(struct IMSTREAM *s)
{
	assert(s);
	ims_drop(s, s->size);
}

/* get flat ptr and size */
ilong ims_flat(const struct IMSTREAM *s, void **pointer)
{
	struct IMSPAGE *current;
	if (s->size == 0) {
		if (pointer) pointer[0] = NULL;
		return 0;
	}
	current = ilist_entry(s->head.next, struct IMSPAGE, head);
	if (pointer) pointer[0] = current->data + s->pos_read;

	if (current->head.next != &s->head) 
		return current->size - s->pos_read;

	return s->pos_write - s->pos_read;
}

/* move data from source to destination */
ilong ims_move(struct IMSTREAM *dst, struct IMSTREAM *src, ilong size)
{
	ilong total = 0;
	while (size > 0) {
		void *ptr;
		ilong canread = ims_flat(src, &ptr);
		ilong toread = (size <= canread)? size : canread;
		ilong readed = 0;
		if (canread <= 0) break;
		readed = ims_write(dst, ptr, toread);
		assert(readed == toread);
		ims_drop(src, readed);
		total += readed;
		size -= readed;
	}
	return total;
}



/*====================================================================*/
/* Common string operation (not be defined in some compiler)          */
/*====================================================================*/

/* strcasestr */
char* istrcasestr(char* s1, char* s2)  
{  
	char* ptr = s1;  
	if (!s1 || !s2 || !*s2) return s1;  
	   
	while (*ptr) {  
		if (ITOUPPER(*ptr) == ITOUPPER(*s2)) {  
			char* cur1 = ptr + 1;  
			char* cur2 = s2 + 1;  
			while (*cur1 && *cur2 && ITOUPPER(*cur1) == ITOUPPER(*cur2)) {  
				cur1++;  
				cur2++;  
			}  
			if (!*cur2) return ptr;  
		}  
		ptr++;  
	}  
	return   NULL;  
}

/* strncasecmp */
int istrncasecmp(char* s1, char* s2, size_t num)
{
	char c1, c2;
	assert(s1 && s2);
	if(!s1|| !s2 || num == 0) return 0;
	while(num > 0){
		c1 = ITOUPPER(*s1);
		c2 = ITOUPPER(*s2);
		if (c1 - c2) return c1 - c2;
		num--;
		s1++;
		s2++;
	}
	return 0;
}

/* strsep */
char *istrsep(char **stringp, const char *delim)
{
	char *s;
	const char *spanp;
	int c, sc;
	char *tok;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0) s = NULL;
				else s[-1] = 0;
				*stringp = s;
				return tok;
			}
		}	while (sc != 0);
	}
}

/* istrtoxl macro */
#define IFL_NEG			1
#define IFL_READDIGIT	2
#define IFL_OVERFLOW	4
#define IFL_UNSIGNED	8

/* istrtoxl */
static unsigned long istrtoxl(const char *nptr, const char **endptr,
	int ibase, int flags)
{
	const char *p;
	char c;
	unsigned long number;
	unsigned long digval;
	unsigned long maxval;
	unsigned long limit;

	if (endptr != NULL) *endptr = nptr;
	assert(ibase == 0 || (ibase >= 2 && ibase <= 36));

	p = nptr;
	number = 0;

	c = *p++;
	while (isspace((int)(IUINT8)c)) c = *p++;

	if (c == '+') c = *p++;
	if (c == '-') {
		flags |= IFL_NEG;
		c = *p++;
	}

	if (c == '+') c = *p++;

	if (ibase < 0 || ibase == 1 || ibase > 36) {
		if (endptr) *endptr = nptr;
		return 0;
	}

	if (ibase == 0) {
		if (c != '0') ibase = 10;
		else if (*p == 'x' || *p == 'X') ibase = 16;
		else if (*p == 'b' || *p == 'B') ibase = 2;
		else ibase = 8;
	}

	if (ibase == 16) {
		if (c == '0' && (*p == 'x' || *p == 'X')) {
			p++;
			c = *p++;
		}
	}
	else if (ibase == 2) {
		if (c == '0' && (*p == 'b' || *p == 'B')) {
			p++;
			c = *p++;
		}
	}

	maxval = (~0ul) / ibase;

	for (; ; ) {
		if (isdigit((int)(IUINT8)c)) digval = c - '0';
		else if (isalpha((int)(IUINT8)c)) 
			digval = ITOUPPER(c) - 'A' + 10;
		else break;

		if (digval >= (unsigned long)ibase) break;

		flags |= IFL_READDIGIT;
	
		if (number < maxval || (number == maxval && 
			(unsigned long)digval <= ~0ul / ibase)) {
			number = number * ibase + digval;
		}	else {
			flags |= IFL_OVERFLOW;
			if (endptr == NULL) {
				break;
			}
		}

		c = *p++;
	}

	--p;

	limit = ((unsigned long)ILONG_MAX) + 1;

	if (!(flags & IFL_READDIGIT)) {
		if (endptr) *endptr = nptr;
		number = 0;
	}
	else if ((flags & IFL_UNSIGNED) && (flags & IFL_NEG)) {
		number = 0;
	}
	else if ((flags & IFL_OVERFLOW) || 
		(!(flags & IFL_UNSIGNED) &&
		(((flags & IFL_NEG) && (number > limit)) || 
		(!(flags & IFL_NEG) && (number > limit - 1))))) {
		if (flags & IFL_UNSIGNED) number = ~0ul;
		else if (flags & IFL_NEG) number = (unsigned long)ILONG_MIN;
		else number = (unsigned long)ILONG_MAX;
	}

	if (endptr) *endptr = p;

	if (flags & IFL_NEG)
		number = (unsigned long)(-(long)number);

	return number;
}

/* istrtoxl */
static IUINT64 istrtoxll(const char *nptr, const char **endptr,
	int ibase, int flags)
{
	const char *p;
	char c;
	IUINT64 number;
	IUINT64 digval;
	IUINT64 maxval;
	IUINT64 limit;

	if (endptr != NULL) *endptr = nptr;
	assert(ibase == 0 || (ibase >= 2 && ibase <= 36));

	p = nptr;
	number = 0;

	c = *p++;
	while (isspace((int)(IUINT8)c)) c = *p++;

	if (c == '+') c = *p++;
	if (c == '-') {
		flags |= IFL_NEG;
		c = *p++;
	}

	if (c == '+') c = *p++;

	if (ibase < 0 || ibase == 1 || ibase > 36) {
		if (endptr) *endptr = nptr;
		return 0;
	}

	if (ibase == 0) {
		if (c != '0') ibase = 10;
		else if (*p == 'x' || *p == 'X') ibase = 16;
		else if (*p == 'b' || *p == 'B') ibase = 2;
		else ibase = 8;
	}

	if (ibase == 16) {
		if (c == '0' && (*p == 'x' || *p == 'X')) p++, c = *p++;
	}
	else if (ibase == 2) {
		if (c == '0' && (*p == 'b' || *p == 'B')) p++, c = *p++;
	}

	maxval = (~((IUINT64)0)) / ibase;

	for (; ; ) {
		if (isdigit((int)(IUINT8)c)) digval = c - '0';
		else if (isalpha((int)(IUINT8)c)) digval = ITOUPPER(c) - 'A' + 10;
		else break;

		if (digval >= (IUINT64)ibase) break;
		flags |= IFL_READDIGIT;
	
		if (number < maxval || (number == maxval && 
			(IUINT64)digval <= ~((IUINT64)0) / ibase)) {
			number = number * ibase + digval;
		}	else {
			flags |= IFL_OVERFLOW;
			if (endptr == NULL) break;
		}

		c = *p++;
	}

	--p;

	limit = ((IUINT64)IINT64_MAX) + 1;

	if (!(flags & IFL_READDIGIT)) {
		if (endptr) *endptr = nptr;
		number = 0;
	}
	else if ((flags & IFL_UNSIGNED) && (flags & IFL_NEG)) {
		number = 0;
	}
	else if ((flags & IFL_OVERFLOW) || 
		(!(flags & IFL_UNSIGNED) &&
		(((flags & IFL_NEG) && (number > limit)) || 
		(!(flags & IFL_NEG) && (number > limit - 1))))) {
		if (flags & IFL_UNSIGNED) number = ~((IUINT64)0);
		else if (flags & IFL_NEG) number = (IUINT64)IINT64_MIN;
		else number = (IUINT64)IINT64_MAX;
	}

	if (endptr) *endptr = p;

	if (flags & IFL_NEG)
		number = (IUINT64)(-(IINT64)number);

	return number;
}

/* ixtoa */
static int ixtoa(IUINT64 val, char *buf, unsigned radix, int is_neg)
{
	IUINT64 digval;
	char *firstdig, *p;
	char temp;
	int size = 0;

	p = buf;
	if (is_neg) {
		if (buf) *p++ = '-';
		size++;
		val = (IUINT64)(-(IINT64)val);
	}

	firstdig = p;

	do {
		digval = (IUINT64)(val % (int)radix);
		val /= (int)radix;
		if (digval > 9 && buf) *p++ = (char)(digval - 10 + 'a');
		else if (digval <= 9 && buf) *p++ = (char)(digval + '0');
		size++;
	}	while (val > 0);

	if (buf == NULL) {
		return size;
	}

	*p-- = '\0';
	do { 
		temp = *p;
		*p = *firstdig;
		*firstdig = temp;
		--p;
		++firstdig;
	}	while (firstdig < p);

	return 0;
}

/* istrtol */
long istrtol(const char *nptr, const char **endptr, int ibase)
{
	return (long)istrtoxl(nptr, endptr, ibase, 0);
}

/* istrtoul */
unsigned long istrtoul(const char *nptr, const char **endptr, int ibase)
{
	return istrtoxl(nptr, endptr, ibase, IFL_UNSIGNED);
}

/* istrtoll */
IINT64 istrtoll(const char *nptr, const char **endptr, int ibase)
{
	return (IINT64)istrtoxll(nptr, endptr, ibase, 0);
}

/* istrtoull */
IUINT64 istrtoull(const char *nptr, const char **endptr, int ibase)
{
	return istrtoxll(nptr, endptr, ibase, IFL_UNSIGNED);
}

/* iltoa */
int iltoa(long val, char *buf, int radix)
{
	IINT64 mval = val;
	return ixtoa((IUINT64)mval, buf, (unsigned)radix, (val < 0)? 1 : 0);
}

/* iultoa */
int iultoa(unsigned long val, char *buf, int radix)
{
	IUINT64 mval = (IUINT64)val;
	return ixtoa(mval, buf, (unsigned)radix, 0);
}

/* iltoa */
int illtoa(IINT64 val, char *buf, int radix)
{
	IUINT64 mval = (IUINT64)val;
	return ixtoa(mval, buf, (unsigned)radix, (val < 0)? 1 : 0);
}

/* iultoa */
int iulltoa(IUINT64 val, char *buf, int radix)
{
	return ixtoa(val, buf, (unsigned)radix, 0);
}

/* istrstrip */
char *istrstrip(char *ptr, const char *delim)
{
	size_t size, i;
	char *p = ptr;
	const char *spanp;

	assert(ptr && delim);

	size = strlen(ptr);

	while (size > 0) {
		for (spanp = delim; *spanp; spanp++) {
			if (*spanp == ptr[size - 1]) break;
		}
		if (*spanp == '\0') break;
		size--;
	}
	
	ptr[size] = 0;

	while (p[0]) {
		for (spanp = delim; *spanp; spanp++) {
			if (*spanp == p[0]) break;
		}
		if (*spanp == '\0') break;
		p++;
	}

	if (p == ptr) return ptr;
	for (i = 0; p[i]; i++) ptr[i] = p[i];
	ptr[i] = '\0';

	return ptr;
}

/* str escape */
ilong istrsave(const char *src, ilong size, char *out)
{
	const IUINT8 *ptr = (const IUINT8*)src;
	IUINT8 *output = (IUINT8*)out;
	ilong length, i;

	if (size < 0) size = strlen(src);

	if (out == NULL) {
		length = 0;
		for (i = 0; i < size; i++) {
			IUINT8 ch = ptr[i];
			if (ch == '\r' || ch == '\n' || ch == '\t') length += 2;
				else if (ch == '\"') length += 2;
			else if (ch == '\\') length += 2;
			else if (ch < 32) length += 4;
			else length++;
		}
		return length + 3;
	}
	else {
		static const char hex[] = "0123456789ABCDEF";
		for (i = 0; i < size; i++) {
			IUINT8 ch = *ptr++;
			if (ch == '\r') *output++ = '\\', *output++ = 'r';
			else if (ch == '\n') *output++ = '\\', *output++ = 'n';
			else if (ch == '\t') *output++ = '\\', *output++ = 't';
			else if (ch == '"') *output++ = '"', *output++ = '"';
			else if (ch == '\\') *output++ = '\\', *output++ = '\\';
			else if (ch < 32) {
				*output++ = '\\';
				*output++ = 'x';
				*output++ = (IUINT8)hex[ch >> 4];
				*output++ = (IUINT8)hex[ch & 15];
			}
			else {
				*output++ = ch;
			}
		}
		length = (ilong)(output - (IUINT8*)out);
		*output++ = 0;
		return length;
	}
}

/* str un-escape */
ilong istrload(const char *src, ilong size, char *out)
{
	const IUINT8 *ptr = (const IUINT8*)src;
	IUINT8 *output = (IUINT8*)out;
	IUINT8 ch;
	ilong i;

	if (size < 0) size = strlen(src);

	if (out == NULL) 
		return size + 1;

	for (i = 0; i < size; ) {
		ch = ptr[i];
		if (ch == '\\') {
			if (i < size - 1) {
				ch = ptr[i + 1];
				switch (ch) {
				case 'r': *output++ = '\r'; i += 2; break;
				case 'n': *output++ = '\n'; i += 2; break;
				case 't': *output++ = '\t'; i += 2; break;
				case '\'': *output++ = '\''; i += 2; break;
				case '\"': *output++ = '\"'; i += 2; break;
				case '\\': *output++ = '\\'; i += 2; break;
				case '0': *output++ = '\0'; i += 2; break;
				case 'x': 
				case 'X':
					if (i < size - 3) {
						IUINT8 a = ptr[i + 2], b = ptr[i + 3], c = 0, d = 0;
						if (a >= '0' && a <= '9') c = a - '0';
						else if (a >= 'a' && a <= 'f') c = a - 'a' + 10;
						else if (a >= 'A' && a <= 'F') c = a - 'A' + 10;
						if (b >= '0' && b <= '9') d = b - '0';
						else if (b >= 'a' && b <= 'f') d = b - 'a' + 10;
						else if (b >= 'A' && b <= 'F') d = b - 'A' + 10;
						*output++ = (c << 4) | d;
						i += 4;
					}	else {
						*output++ = '\\';
						i++;
					}
					break;
				default:
					*output++ = '\\';
					i++;
					break;
				}
			}	else {
				*output++ = '\\';
				i++;
			}
		}
		else if (ch == '"') {
			if (i < size - 1) {
				ch = ptr[i + 1];
				if (ch == '"') *output++ = '\"', i += 2;
				else *output++ = '\"', i += 1;
			}	else {
				*output++ = '"';
				i++;
			}
		}
		else {
			*output++ = ch;
			i++;
		}
	}
	size = (ilong)(output - (IUINT8*)out);
	*output++ = '\0';
	return size;
}

/* csv tokenizer */
const char *istrcsvtok(const char *text, ilong *next, ilong *size)
{
	ilong begin = 0, endup = 0, i;
	int quotation = 0;

	if (*next < 0) {
		*size = 0;
		return NULL;
	}

	begin = *next;

	if (text[begin] == 0) {
		*size = 0;
		*next = -1;
		if (begin == 0) return NULL;
		if (text[begin - 1] == ',') return text + begin;
		return NULL;
	}

	for (i = begin, endup = begin; ; ) {
		if (quotation == 0) {
			if (text[i] == ',') { endup = i; *next = i + 1; break; }
			else if (text[i] == '\0') { endup = i; *next = i; break; }
			else if (text[i] == '"') quotation = 1, i++;
			else i++;
		}
		else {
			if (text[i] == '\0') { endup = i; *next = i; break; }
			if (text[i] == '"') { 
				if (text[i + 1] == '"') i += 2;
				else i++, quotation = 0;
			}	else {
				i++;
			}
		}
	}

	*size = endup - begin;
	return text + begin;
}



/*====================================================================*/
/* BASE64 / BASE32 / BASE16                                           */
/*====================================================================*/

/* encode data as a base64 string, returns string size,
   if dst == 0, returns how many bytes needed for encode (>=real) */
ilong ibase64_encode(const void *src, ilong size, char *dst)
{
	const IUINT8 *s = (const IUINT8*)src;
	static const char encode[] = 
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	iulong c;
	char *d = dst;
	int i;

	if (size == 0) return 0;

	/* returns nbytes needed */
	if (src == NULL || dst == NULL) {
		ilong nchars, result;
		nchars = ((size + 2) / 3) * 4;
		result = nchars + ((nchars - 1) / 76) + 1;
		return result;
	}

	for (i = 0; i < size; ) {
		c = s[i]; 
		c <<= 8;
		i++;
		c += (i < size)? s[i] : 0;
		c <<= 8;
		i++;
		c += (i < size)? s[i] : 0;
		i++;
		*d++ = encode[(c >> 18) & 0x3f];
		*d++ = encode[(c >> 12) & 0x3f];
		*d++ = (i > (size + 1))? '=' : encode[(c >> 6) & 0x3f];
		*d++ = (i > (size + 0))? '=' : encode[(c >> 0) & 0x3f];
	}

	d[0] = '\0';
	return (ilong)(d - dst);
}

/* decode a base64 string into data, returns data size */
ilong ibase64_decode(const char *src, ilong size, void *dst)
{
	static iulong decode[256] = { 0xff };
	const IUINT8 *s = (const IUINT8*)src;
	IUINT8 *d = (IUINT8*)dst;
	iulong mark, i, j, c, k;
	char b[3];

	if (size == 0) return 0;
	if (size < 0) size = strlen(src);

	if (src == NULL || dst == NULL) {
		ilong nbytes;
		nbytes = ((size + 7) / 4) * 3;
		return nbytes;
	}

	if (decode[0] == 0xff) {
		for (i = 1; i < 256; i++) {
			if (i >= 'A' && i <= 'Z') decode[i] = i - 'A';
			else if (i >= 'a' && i <= 'z') decode[i] = i - 'a' + 26;
			else if (i >= '0' && i <= '9') decode[i] = i - '0' + 52;
			else if (i == '+') decode[i] = 62;
			else if (i == '/') decode[i] = 63;
			else if (i == '=') decode[i] = 0;
			else decode[i] = 88;
		}
		decode[0] = 88;
	}

	#define ibase64_skip(s, i, size) {					\
			for (; i < size; i++) {						\
				if (decode[s[i]] <= 64) break;			\
			}											\
			if (i >= size) { i = size + 1; break; } 	\
		}
	
	for (i = 0, j = 0, k = 0; i < (iulong)size; ) {
		mark = 0;
		c = 0;

		ibase64_skip(s, i, (iulong)size);
		c += decode[s[i]];
		c <<= 6;
		i++;
		
		ibase64_skip(s, i, (iulong)size);
		c += decode[s[i]];
		c <<= 6;
		i++;

		ibase64_skip(s, i, (iulong)size);
		if (s[i] != '=') {
			c += decode[s[i]];
			c <<= 6;
			i++;
			ibase64_skip(s, i, (iulong)size);
			if (s[i] != '=') c += decode[s[i]], i++;
			else i = size, mark = 1;
		}	else {
			i = size;
			mark = 2;
			c <<= 6;
		}

		b[0] = (IUINT8)((c >> 16) & 0xff);
		b[1] = (IUINT8)((c >>  8) & 0xff);
		b[2] = (IUINT8)((c >>  0) & 0xff);

		for (j = 0; j < 3 - mark; j++) 
			d[k++] = b[j];
	}

	return (ilong)k;
}

/* encode data as a base32 string, returns string size */
ilong ibase32_encode(const void *src, ilong size, char *dst)
{
	static const char encode[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
	const IUINT8 *buffer = (const IUINT8*)src;
	IUINT8 word;
	ilong i, index;
	char *ptr = dst;

	if (size == 0) return 0;

	if (src == NULL || dst == NULL) {
		ilong nchars, result;
		nchars = ((size + 4) / 5) * 8;
		result = nchars + ((nchars - 1) / 76) + 1;
		return result;
	}

	for (i = 0, index = 0; i < size; ) {
		if (index > 3) {
			word = (buffer[i] & (0xFF >> index));
			index = (index + 5) % 8;
			word <<= index;
			if (i < size - 1) {
				word |= buffer[i + 1] >> (8 - index);
			}
			i++;
		}	else {
			word = (buffer[i] >> (8 - (index + 5))) & 0x1F;
			index = (index + 5) & 7;
			if (index == 0) i++;
		}

		assert(word < 32);
		*(dst++) = (char)encode[word];
	}

	while ((((ilong)(dst - ptr)) & 7) != 0) *dst++ = '=';

	*dst = 0;

	return (ilong)(dst - ptr);
}

/* decode a base32 string into data, returns data size */
ilong ibase32_decode(const char *src, ilong size, void *dst)
{
	const IUINT8 *lptr = (const IUINT8*)src;
	IUINT8 *buffer = (IUINT8*)dst;
	IUINT8 word;
	ilong offset, last, i;
	int index;

	if (size == 0) return 0;
	if (size < 0) size = strlen(src);

	if (src == NULL || dst == NULL) {
		ilong need = ((size + 15) / 8) * 5;
		return need;
	}

	for(i = 0, index = 0, offset = 0, last = -1; i < size; i++) {
		IUINT8 ch = lptr[i];

		if (ch >= '2' && ch <= '7') word = ch - '2' + 26;
		else if (ch >= 'A' && ch <= 'Z') word = ch - 'A';
		else if (ch >= 'a' && ch <= 'z') word = ch - 'a';
		else continue;

		if (index <= 3) {
			index = (index + 5) & 7;
			if (index == 0) {
				if (last < offset) buffer[offset] = 0, last = offset;
				buffer[offset] |= word;
				offset++;
			}	else {
				if (last < offset) buffer[offset] = 0, last = offset;
				buffer[offset] |= word << (8 - index);
			}
		}	else {
			index = (index + 5) & 7;
			if (last < offset) buffer[offset] = 0, last = offset;
			buffer[offset] |= (word >> index);
			offset++;
			buffer[offset] = word << (8 - index);
			last = offset;
		}
	}

	return offset;
}

/* encode data as a base16 string, returns string size */
ilong ibase16_encode(const void *src, ilong size, char *dst)
{
	static const char encode[] = "0123456789ABCDEF";
	const IUINT8 *ptr = (const IUINT8*)src;
	char *output = dst;
	if (src == NULL || dst == NULL) 
		return 2 * size;
	for (; size > 0; output += 2, ptr++, size--) {
		output[0] = encode[ptr[0] >> 4];
		output[1] = encode[ptr[0] & 15];
	}
	return (ilong)(output - dst);
}

/* decode a base16 string into data, returns data size */
ilong ibase16_decode(const char *src, ilong size, void *dst)
{
	const IUINT8 *in = (const IUINT8*)src;
	IUINT8 *out = (IUINT8*)dst, word = 0, decode = 0;
	int index = 0;

	if (size == 0) return 0;
	if (size < 0) size = strlen(src);

	if (src == NULL || dst == NULL) 
		return size >> 1;
	
	for (; size > 0; size--) {
		IUINT8 ch = *in++;
		if (ch >= '0' && ch <= '9') word = ch - '0';
		else if (ch >= 'A' && ch <= 'F') word = ch - 'A' + 10;
		else if (ch >= 'a' && ch <= 'f') word = ch - 'a' + 10;
		else continue;
		if (index == 0) decode = word << 4, index = 1;
		else decode |= word & 0xf, *out++ = decode, index = 0;
	}

	return (ilong)(out - (IUINT8*)dst);
}


/*====================================================================*/
/* RC4                                                                */
/*====================================================================*/

/* rc4 init */
void icrypt_rc4_init(unsigned char *box, int *x, int *y, 
	const unsigned char *key, int keylen)
{
	int X, Y, i, j, k, a;
	if (keylen <= 0 || key == NULL) {
		X = -1;
		Y = -1;
	}	else {
		X = Y = j = k = 0;
		for (i = 0; i < 256; i++) 
			box[i] = (unsigned char)i;
		for (i = 0; i < 256; i++) {
			a = box[i];
			j = (unsigned char)(j + a + key[k]);
			box[i] = box[j];
			box[j] = a;
			if (++k >= keylen) k = 0;
		}
	}
	x[0] = X;
	y[0] = Y;
}

/* rc4_crypt */
void icrypt_rc4_crypt(unsigned char *box, int *x, int *y, 
	const unsigned char *src, unsigned char *dst, ilong size)
{
	int X = x[0];
	int Y = y[0];
	if (X < 0 || Y < 0) {			/* no crypt */
		if (src != dst) 
			memmove(dst, src, size);
	}	else {						/* crypt */
		int a, b; 
		for (; size > 0; src++, dst++, size--) {
			X = (unsigned char)(X + 1);
			a = box[X];
			Y = (unsigned char)(Y + a);
			box[X] = box[Y];
			b = box[Y];
			box[Y] = a;
			dst[0] = src[0] ^ box[(unsigned char)(a + b)];
		}
		x[0] = X;
		y[0] = Y;
	}
}


/*====================================================================*/
/* UTF-8/16/32 conversion                                             */
/*====================================================================*/

#define ICONV_REPLACEMENT_CHAR  ((IUINT32)0x0000FFFD)
#define ICONV_MAX_BMP           ((IUINT32)0x0000FFFF)
#define ICONV_MAX_UTF16         ((IUINT32)0x0010FFFF)
#define ICONV_MAX_UTF32         ((IUINT32)0x7FFFFFFF)
#define ICONV_MAX_LEGAL_UTF32   ((IUINT32)0x0010FFFF)

#define ICONV_SUR_HIGH_START    ((IUINT32)0xD800)
#define ICONV_SUR_HIGH_END      ((IUINT32)0xDBFF)
#define ICONV_SUR_LOW_START     ((IUINT32)0xDC00)
#define ICONV_SUR_LOW_END       ((IUINT32)0xDFFF)

#define ICONV_IS_OK             (0)
#define ICONV_SRC_EXHAUSTED     (-1)
#define ICONV_TARGET_EXHAUSTED  (-2)
#define ICONV_INVALID_CHAR      (-3)

static const int ihalfShift  = 10; /* used for shifting by 10 bits */
static const IUINT32 ihalfBase = 0x0010000UL;
static const IUINT32 ihalfMask = 0x3FFUL;

static const char iconv_utf8_trailing[256] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

static const IUINT32 iconv_utf8_offset[6] = { 0x00000000UL, 
	0x00003080UL, 0x000E2080UL, 0x03C82080UL, 0xFA082080UL, 
	0x82082080UL };

static const IUINT32 iconv_first_mark[7] = { 0x00, 0x00, 0xC0,
	0xE0, 0xF0, 0xF8, 0xFC };

/* check if a UTF-8 character is legal */
static inline int iposix_utf8_legal(const IUINT8 *source, int length) {
	const IUINT8 *srcptr = source + length;
	IUINT8 a;
	switch (length) {
		default: return 0;
				 /* Everything else falls through when "true"... */
		case 4: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
		case 3: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
		case 2: if ((a = (*--srcptr)) > 0xBF) return 0;
					switch (*source) {
						/* no fall-through in this inner switch */
						case 0xE0: if (a < 0xA0) return 0; break;
						case 0xED: if (a > 0x9F) return 0; break;
						case 0xF0: if (a < 0x90) return 0; break;
						case 0xF4: if (a > 0x8F) return 0; break;
						default:   if (a < 0x80) return 0;
					}
		case 1: if (*source >= 0x80 && *source < 0xC2) return 0;
	}
	if (*source > 0xF4) return 0;
	return 1;
}

/* check if a UTF-8 character is legal, returns 1 for legal, 0 for illegal */
int iposix_utf_check8(const IUINT8 *source, const IUINT8 *srcEnd) {
	int length = iconv_utf8_trailing[*source] + 1;
	if (source + length > srcEnd) {
		return 0;
	}
	return iposix_utf8_legal(source, length);
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for destination exhausted, -3 for invalid character */
int iposix_utf_8to16(const IUINT8 **srcStart, const IUINT8 *srcEnd,
		IUINT16 **targetStart, IUINT16 *targetEnd, int strict)
{
	int result = 0;
	const IUINT8* source = *srcStart;
	IUINT16* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch = 0;
		unsigned short extraBytesToRead = iconv_utf8_trailing[*source];
		if (source + extraBytesToRead >= srcEnd) {
			result = ICONV_SRC_EXHAUSTED; break;
		}
		/* Do this check whether lenient or strict */
		if (! iposix_utf8_legal(source, extraBytesToRead+1)) {
			result = ICONV_INVALID_CHAR;
			break;
		}
		/*
		 * The cases all fall through. See "Note A" below.
		 */
		switch (extraBytesToRead) {
			case 5: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
			case 4: ch += *source++; ch <<= 6; /* remember, illegal UTF-8 */
			case 3: ch += *source++; ch <<= 6;
			case 2: ch += *source++; ch <<= 6;
			case 1: ch += *source++; ch <<= 6;
			case 0: ch += *source++;
		}
		ch -= iconv_utf8_offset[extraBytesToRead];
		if (target >= targetEnd) {
			source -= (extraBytesToRead+1); /* Back up source pointer! */
			result = ICONV_TARGET_EXHAUSTED; break;
		}
		if (ch <= ICONV_MAX_BMP) { /* Target is a character <= 0xFFFF */
			/* UTF-16 surrogate values are illegal in UTF-32 */
			if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_LOW_END) {
				if (strict) {
					/* return to the illegal value itself */
					source -= (extraBytesToRead+1);
					result = ICONV_INVALID_CHAR;
					break;
				} else {
					*target++ = ICONV_REPLACEMENT_CHAR;
				}
			} else {
				*target++ = (IUINT16)ch; /* normal case */
			}
		} else if (ch > ICONV_MAX_UTF16) {
			if (strict) {
				result = ICONV_INVALID_CHAR;
				source -= (extraBytesToRead+1); /* return to the start */
				break; /* Bail out; shouldn't continue */
			} else {
				*target++ = ICONV_REPLACEMENT_CHAR;
			}
		} else {
			/* target is a character in range 0xFFFF - 0x10FFFF. */
			if (target + 1 >= targetEnd) {
				/* Back up source pointer! */
				source -= (extraBytesToRead+1);
				result = ICONV_TARGET_EXHAUSTED; break;
			}
			ch -= ihalfBase;
			*target++ = (IUINT16)((ch >> ihalfShift) + ICONV_SUR_HIGH_START);
			*target++ = (IUINT16)((ch & ihalfMask) + ICONV_SUR_LOW_START);
		}
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}


/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_8to32(const IUINT8 **srcStart, const IUINT8 *srcEnd,
		IUINT32 **targetStart, IUINT32 *targetEnd, int strict)
{
	int result = 0;
	const IUINT8* source = *srcStart;
	IUINT32* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch = 0;
		unsigned short extraBytesToRead = iconv_utf8_trailing[*source];
		if (source + extraBytesToRead >= srcEnd) {
			result = ICONV_SRC_EXHAUSTED; break;
		}
		/* Do this check whether lenient or strict */
		if (! iposix_utf8_legal(source, extraBytesToRead+1)) {
			result = ICONV_INVALID_CHAR;
			break;
		}
		/*
		 * The cases all fall through. See "Note A" below.
		 */
		switch (extraBytesToRead) {
			case 5: ch += *source++; ch <<= 6;
			case 4: ch += *source++; ch <<= 6;
			case 3: ch += *source++; ch <<= 6;
			case 2: ch += *source++; ch <<= 6;
			case 1: ch += *source++; ch <<= 6;
			case 0: ch += *source++;
		}
		ch -= iconv_utf8_offset[extraBytesToRead];
		if (target >= targetEnd) {
			/* Back up the source pointer! */
			source -= (extraBytesToRead+1); 
			result = ICONV_TARGET_EXHAUSTED; break;
		}
		if (ch <= ICONV_MAX_LEGAL_UTF32) {
			/*
			 * UTF-16 surrogate values are illegal in UTF-32, and anything
			 * over Plane 17 (> 0x10FFFF) is illegal.
			 */
			if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_LOW_END) {
				if (strict) {
					/* return to the illegal value itself */
					source -= (extraBytesToRead+1); 
					result = ICONV_INVALID_CHAR;
					break;
				} else {
					*target++ = ICONV_REPLACEMENT_CHAR;
				}
			} else {
				*target++ = ch;
			}
		} else { /* i.e., ch > ICONV_MAX_LEGAL_UTF32 */
			result = ICONV_INVALID_CHAR;
			*target++ = ICONV_REPLACEMENT_CHAR;
		}
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_16to8(const IUINT16 **srcStart, const IUINT16 *srcEnd,
		IUINT8 **targetStart, IUINT8 *targetEnd, int strict)
{
	int result = 0;
	const IUINT16* source = *srcStart;
	IUINT8* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch;
		unsigned short bytesToWrite = 0;
		const IUINT32 byteMask = 0xBF;
		const IUINT32 byteMark = 0x80; 
		/* In case we have to back up because of target overflow. */
		const IUINT16* oldSource = source; 
		ch = *source++;
		/* If we have a surrogate pair, convert to IUINT32 first. */
		if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_HIGH_END) {
			/* If the 16 bits following the high surrogate are in 
			 * the source buffer... */
			if (source < srcEnd) {
				IUINT32 ch2 = *source;
				/* If it's a low surrogate, convert to IUINT32. */
				if (ch2 >= ICONV_SUR_LOW_START && ch2 <= ICONV_SUR_LOW_END) {
					ch = ((ch - ICONV_SUR_HIGH_START) << ihalfShift)
						+ (ch2 - ICONV_SUR_LOW_START) + ihalfBase;
					++source;
				} else if (strict) { /* it's an unpaired high surrogate */
					--source; /* return to the illegal value itself */
					result = ICONV_INVALID_CHAR;
					break;
				}
			} else { 
				/* We don't have the 16 bits following the high surrogate. */
				--source; /* return to the high surrogate */
				result = ICONV_SRC_EXHAUSTED;
				break;
			}
		} else if (strict) {
			/* UTF-16 surrogate values are illegal in UTF-32 */
			if (ch >= ICONV_SUR_LOW_START && ch <= ICONV_SUR_LOW_END) {
				--source; /* return to the illegal value itself */
				result = ICONV_INVALID_CHAR;
				break;
			}
		}
		/* Figure out how many bytes the result will require */
		if (ch < (IUINT32)0x80) {
			bytesToWrite = 1;
		} 
		else if (ch < (IUINT32)0x800) {
			bytesToWrite = 2;
		}
		else if (ch < (IUINT32)0x10000) { 
			bytesToWrite = 3;
		}
		else if (ch < (IUINT32)0x110000) {
			bytesToWrite = 4;
		}
		else {
			bytesToWrite = 3;
			ch = ICONV_REPLACEMENT_CHAR;
		}
		target += bytesToWrite;
		if (target > targetEnd) {
			source = oldSource; /* Back up source pointer! */
			target -= bytesToWrite; result = ICONV_TARGET_EXHAUSTED; break;
		}
		switch (bytesToWrite) { /* note: everything falls through. */
		case 4: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
		case 3: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
		case 2: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
		case 1: *--target = (IUINT8)(ch | iconv_first_mark[bytesToWrite]);
		}
		target += bytesToWrite;
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_16to32(const IUINT16 **srcStart, const IUINT16 *srcEnd,
		IUINT32 **targetStart, IUINT32 *targetEnd, int strict)
{
	int result = 0;
	const IUINT16* source = *srcStart;
	IUINT32* target = *targetStart;
	IUINT32 ch = 0, ch2 = 0;
	while (source < srcEnd) {
		/*  In case we have to back up because of target overflow. */
		const IUINT16* oldSource = source;
		ch = *source++;
		/* If we have a surrogate pair, convert to IUINT32 first. */
		if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_HIGH_END) {
			/* If the 16 bits following the high surrogate are in 
			 * the source buffer... */
			if (source < srcEnd) {
				ch2 = *source;
				/* If it's a low surrogate, convert to IUINT32. */
				if (ch2 >= ICONV_SUR_LOW_START && ch2 <= ICONV_SUR_LOW_END) {
					ch = ((ch - ICONV_SUR_HIGH_START) << ihalfShift)
						+ (ch2 - ICONV_SUR_LOW_START) + ihalfBase;
					++source;
				} else if (strict) { /* it's an unpaired high surrogate */
					--source; /* return to the illegal value itself */
					result = ICONV_INVALID_CHAR;
					break;
				}
			} else {
				/* We don't have the 16 bits following the high surrogate. */
				--source; /* return to the high surrogate */
				result = ICONV_SRC_EXHAUSTED;
				break;
			}
		} else if (strict) {
			/* UTF-16 surrogate values are illegal in UTF-32 */
			if (ch >= ICONV_SUR_LOW_START && ch <= ICONV_SUR_LOW_END) {
				--source; /* return to the illegal value itself */
				result = ICONV_INVALID_CHAR;
				break;
			}
		}
		if (target >= targetEnd) {
			source = oldSource; /* Back up source pointer! */
			result = ICONV_TARGET_EXHAUSTED; break;
		}
		*target++ = ch;
	}
	*srcStart = source;
	*targetStart = target;
#ifdef ICONV_DEBUG
if (result == ICONV_INVALID_CHAR) {
	fprintf(stderr, "iposix_utf_16to32 illegal seq 0x%04x,%04x\n", ch, ch2);
	fflush(stderr);
}
#endif
	return result;
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_32to8(const IUINT32 **srcStart, const IUINT32 *srcEnd,
		IUINT8 **targetStart, IUINT8 *targetEnd, int strict)
{
	int result = 0;
	const IUINT32* source = *srcStart;
	IUINT8* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch;
		unsigned short bytesToWrite = 0;
		const IUINT32 byteMask = 0xBF;
		const IUINT32 byteMark = 0x80; 
		ch = *source++;
		if (strict) {
			/* UTF-16 surrogate values are illegal in UTF-32 */
			if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_LOW_END) {
				--source; /* return to the illegal value itself */
				result = ICONV_INVALID_CHAR;
				break;
			}
		}
		/*
		 * Figure out how many bytes the result will require. Turn any
		 * illegally large IUINT32 things (> Plane 17) into replacement chars.
		 */
		if (ch < (IUINT32)0x80) {
			bytesToWrite = 1;
		}
		else if (ch < (IUINT32)0x800) {
			bytesToWrite = 2;
		}
		else if (ch < (IUINT32)0x10000) {
			bytesToWrite = 3;
		}
		else if (ch <= ICONV_MAX_LEGAL_UTF32) {
			bytesToWrite = 4;
		}
		else {
			bytesToWrite = 3;
			ch = ICONV_REPLACEMENT_CHAR;
			result = ICONV_INVALID_CHAR;
		}

		target += bytesToWrite;
		if (target > targetEnd) {
			--source; /* Back up source pointer! */
			target -= bytesToWrite; result = ICONV_TARGET_EXHAUSTED; break;
		}
		switch (bytesToWrite) { /* note: everything falls through. */
		case 4: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
		case 3: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
		case 2: *--target = (IUINT8)((ch | byteMark) & byteMask); ch >>= 6;
		case 1: *--target = (IUINT8) (ch | iconv_first_mark[bytesToWrite]);
		}
		target += bytesToWrite;
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}

/* returns 0 for success, -1 for source exhausted,
 * -2 for target exhausted, -3 for invalid character */
int iposix_utf_32to16(const IUINT32 **srcStart, const IUINT32 *srcEnd,
		IUINT16 **targetStart, IUINT16 *targetEnd, int strict)
{
	int result = 0;
	const IUINT32* source = *srcStart;
	IUINT16* target = *targetStart;
	while (source < srcEnd) {
		IUINT32 ch;
		if (target >= targetEnd) {
			result = ICONV_TARGET_EXHAUSTED; break;
		}
		ch = *source++;
		if (ch <= ICONV_MAX_BMP) { /* Target is a character <= 0xFFFF */
			/* UTF-16 surrogate values are illegal in UTF-32; 
			 * 0xffff or 0xfffe are both reserved values */
			if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_LOW_END) {
				if (strict) {
					--source; /* return to the illegal value itself */
					result = ICONV_INVALID_CHAR;
					break;
				} else {
					*target++ = ICONV_REPLACEMENT_CHAR;
				}
			} else {
				*target++ = (IUINT16)ch; /* normal case */
			}
		} else if (ch > ICONV_MAX_LEGAL_UTF32) {
			if (strict) {
				result = ICONV_INVALID_CHAR;
			} else {
				*target++ = ICONV_REPLACEMENT_CHAR;
			}
		} else {
			/* target is a character in range 0xFFFF - 0x10FFFF. */
			if (target + 1 >= targetEnd) {
				--source; /* Back up source pointer! */
				result = ICONV_TARGET_EXHAUSTED; 
				break;
			}
			ch -= ihalfBase;
			*target++ = (IUINT16)((ch >> ihalfShift) + ICONV_SUR_HIGH_START);
			*target++ = (IUINT16)((ch & ihalfMask) + ICONV_SUR_LOW_START);
		}
	}
	*srcStart = source;
	*targetStart = target;
	return result;
}


/* count characters in UTF-8 string, returns -1 for illegal sequence */
int iposix_utf_count8(const IUINT8 *source, const IUINT8 *srcEnd)
{
	int count = 0;
	while (source < srcEnd) {
		unsigned short extraBytesToRead = iconv_utf8_trailing[*source];
		if (source + extraBytesToRead >= srcEnd) {
			return -1; /* source exhausted */
		}
		source += extraBytesToRead + 1;
		count++;
	}
	return count;
}

/* count characters in UTF-16 string, returns -1 for illegal sequence */
int iposix_utf_count16(const IUINT16 *source, const IUINT16 *srcEnd)
{
	int count = 0;
	while (source < srcEnd) {
		IUINT32 ch = *source++;
		if (ch >= ICONV_SUR_HIGH_START && ch <= ICONV_SUR_HIGH_END) {
			if (source < srcEnd) {
				IUINT32 ch2 = *source;
				if (ch2 >= ICONV_SUR_LOW_START && ch2 <= ICONV_SUR_LOW_END) {
					source++; /* valid surrogate pair */
				} else {
					return -1; /* illegal sequence */
				}
			} else {
				return -1; /* source exhausted */
			}
		} else if (ch >= ICONV_SUR_LOW_START && ch <= ICONV_SUR_LOW_END) {
			return -1; /* illegal sequence */
		}
		count++;
	}
	return count;
}


/*====================================================================*/
/* EXTENSION FUNCTIONS                                                */
/*====================================================================*/

/* push message into stream */
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


/* read message from stream */
IINT32 iposix_msg_read(struct IMSTREAM *queue, IINT32 *msg, 
		IINT32 *wparam, IINT32 *lparam, void *data, IINT32 maxsize)
{
	IINT32 length, size, cc;
	char head[16];
	if (queue->size < 16) return -1;
	ims_peek(queue, head, 4);
	idecode32i_lsb(head, &length);
	if (length < 16) {
		assert(length >= 16);
		abort();
	}
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


/**********************************************************************
 * 32 bits incremental hash functions
 **********************************************************************/
IUINT32 inc_hash_crc32_table[256];

/* CRC32 hash table initialize */
void inc_hash_crc32_initialize(void)
{
	static int initialized = 0;
	IUINT32 polynomial = 0xEDB88320;
	IUINT32 i, j, crc;
	if (initialized) return;
	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 0; j < 8; j++) {
			if (crc & 1) {
				crc = (crc >> 1) ^ polynomial;
			} else {
				crc >>= 1;
			}
		}
		inc_hash_crc32_table[i] = crc;
	}
	initialized = 1;
}


/**********************************************************************
 * Dictionary Basic Interface
 **********************************************************************/

/* create */
idict_t *idict_create(void)
{
	idict_t *dict;
	ilong i;
	dict = (idict_t*)ikmem_malloc(sizeof(idict_t));
	if (dict == NULL) return NULL;

	imnode_init(&dict->nodes, sizeof(struct IDICTENTRY), ikmem_allocator);
	iv_init(&dict->vect, ikmem_allocator);

	dict->shift = 6;
	dict->length = ((ilong)1 << dict->shift);
	dict->mask = dict->length - 1;
	dict->size = 0;
	dict->nodes.grow_limit = 8192;

	if (iv_resize(&dict->vect, sizeof(struct IDICTBUCKET) * dict->length)) {
		ikmem_free(dict);
		return NULL;
	}

	dict->table = (struct IDICTBUCKET*)dict->vect.data;
	for (i = 0; i < dict->length; i++) {
		ilist_init(&dict->table[i].head);
		dict->table[i].count = 0;
	}

	for (i = 0; i < (ilong)IDICT_LRUSIZE; i++) 
		dict->lru[i] = NULL;

	dict->inc = 0;
	return dict;
}

/* delete */
void idict_delete(idict_t *dict)
{
	struct IDICTENTRY *entry;
	ilong index;

	assert(dict);
	index = imnode_head(&dict->nodes);
	for (; index >= 0; ) {
		entry = (struct IDICTENTRY*)IMNODE_DATA(&dict->nodes, index);
		ilist_del(&entry->queue);
		it_destroy(&entry->key);
		it_destroy(&entry->val);
		index = imnode_next(&dict->nodes, index);
	}
	iv_destroy(&dict->vect);
	imnode_destroy(&dict->nodes);
	ikmem_free(dict);
}

/* search pair */
static inline idictentry_t *_idict_search(idict_t *dict, const ivalue_t *key)
{
	idictentry_t *recent, *entry;
	struct IDICTBUCKET *bucket;
	ilist_head *head, *p;
	iulong hash1;
	iulong hash2;

	hash1 = key->hash;
	hash2 = ((hash1 & 0xffff) + (hash1 >> 16)) & (IDICT_LRUSIZE - 1);
	recent = dict->lru[hash2];

	if (recent) {
		if (recent->key.hash == hash1) {
			if (it_cmp(&recent->key, key) == 0) 
				return recent;
		}
	}

	bucket = &dict->table[hash1 & dict->mask];
	head = &bucket->head;

	for (p = head->next; p != head; p = p->next) {
		entry = ilist_entry(p, idictentry_t, queue);
		if (entry->key.hash != hash1) continue;
		if (it_cmp(&entry->key, key) == 0) {
			dict->lru[hash2] = entry;
			return entry;
		}
	}

	return NULL;
}

/* reference value and recalculate hash */
static inline void _idict_refval(ivalue_t *dst, const ivalue_t *src)
{
	if (it_type(src) != ITYPE_STR) {
		*dst = *src;
		dst->hash = (iulong)it_int(dst);
		return;
	}
	it_strref(dst, it_str(src), it_size(src));
	dst->rehash = 1;
	dst->hash = src->hash;
	if (it_rehash(src) == 0) {
		it_hashstr(dst);
	}
}

/* search pair */
ivalue_t *idict_search(idict_t *dict, const ivalue_t *key, ilong *pos)
{
	idictentry_t *entry;
	ivalue_t kk;

	_idict_refval(&kk, key);
	entry = _idict_search(dict, &kk);

	if (entry == NULL) 
		return NULL;

	if (pos) pos[0] = entry->pos;

	return &entry->val;
}

/* calculate lru hash */
static inline iulong _idict_lruhash(iulong hash)
{
	return ((hash & 0xffff) + (hash >> 16)) & (IDICT_LRUSIZE - 1);
}

/* resize bucket */
static inline int _idict_resize(idict_t *dict, int newshift)
{
	struct IDICTBUCKET *bucket, *table;
	idictentry_t *entry;
	iulong hash;
	ilong mask, newsize;
	ilong pos, i;
	int retval;

	newsize = ((ilong)1 << newshift);

	retval = iv_resize(&dict->vect, sizeof(struct IDICTBUCKET) * newsize);
	if (retval) return -1;

	dict->table = (struct IDICTBUCKET*)dict->vect.data;

	for (i = 0; i < (ilong)newsize; i++) {
		ilist_init(&dict->table[i].head);
		dict->table[i].count = 0;
	}

	dict->length = newsize;
	dict->shift = newshift;
	dict->mask = newsize - 1;
	mask = dict->mask;
	table = dict->table;

	pos = imnode_head(&dict->nodes);
	for (; pos >= 0; pos = IMNODE_NEXT(&dict->nodes, pos)) {
		entry = (idictentry_t*)IMNODE_DATA(&dict->nodes, pos);
		hash = entry->key.hash;
		bucket = &table[hash & mask];
		bucket->count++;
		ilist_init(&entry->queue);
		ilist_add_tail(&entry->queue, &bucket->head);
	}

	return 0;
}

/* update pair inline */
static inline ilong _idict_update(idict_t *dict, const ivalue_t *key, 
	const ivalue_t *val, int isupdate)
{
	idictentry_t *recent, *entry;
	struct IDICTBUCKET *bucket;
	ilist_head *head, *p;
	iulong hash1;
	iulong hash2;
	ilong pos;

	hash1 = key->hash;
	hash2 = _idict_lruhash(hash1);
	recent = dict->lru[hash2];

	/* check lru cache */
	if (recent) {
		if (recent->key.hash == hash1) {
			if (it_cmp(&recent->key, key) == 0) {
				if (isupdate == 0) return -1;
				it_cpy(&recent->val, val);
				return recent->pos;
			}
		}
	}

	bucket = &dict->table[hash1 & dict->mask];
	head = &bucket->head;

	/* check bucket queue */
	for (p = head->next; p != head; p = p->next) {
		entry = ilist_entry(p, idictentry_t, queue);
		if (entry->key.hash != hash1) continue;
		if (it_cmp(&entry->key, key) == 0) {
			dict->lru[hash2] = entry;
			if (isupdate == 0) return -2;
			it_cpy(&entry->val, val);
			return entry->pos;
		}
	}

	/* new entry */
	pos = imnode_new(&dict->nodes);
	if (pos < 0) return -3;

	entry = (struct IDICTENTRY*)IMNODE_DATA(&dict->nodes, pos);

	/* copy key & value */
	it_init(&entry->key, it_type(key));
	it_init(&entry->val, it_type(val));

	it_cpy(&entry->key, key);
	it_cpy(&entry->val, val);
	entry->key.hash = key->hash;

	entry->pos = pos;
	entry->sid = ++dict->inc;

	/* add to bucket queue */
	ilist_add_tail(&entry->queue, &bucket->head);
	dict->lru[hash2] = entry;

	bucket->count++;
	dict->size++;

	/* check necessary of table-growwing */
	if (dict->size >= (dict->length << 1)) 
		_idict_resize(dict, (int)(dict->shift) + 1);

	return pos;
}

/* add (key, val) pair to dict */
ilong idict_add(idict_t *dict, const ivalue_t *key, const ivalue_t *val)
{
	ivalue_t kk;
	_idict_refval(&kk, key);
	return _idict_update(dict, &kk, val, 0);
}

/* delete pair from dict */
static inline int _idict_del(idict_t *dict, idictentry_t *entry)
{
	iulong hash1, hash2, pos;
	struct IDICTBUCKET *bucket;

	hash1 = entry->key.hash;
	hash2 = _idict_lruhash(hash1);

	bucket = &dict->table[hash1 & dict->mask];
	ilist_del(&entry->queue);

	dict->lru[hash2] = NULL;

	it_destroy(&entry->key);
	it_destroy(&entry->val);
	pos = entry->pos;
	entry->pos = -1;
	entry->sid = -1;

	imnode_del(&dict->nodes, pos);
	bucket->count--;
	dict->size--;

	return 0;
}

/* delete pair from dict */
int idict_del(idict_t *dict, const ivalue_t *key)
{
	idictentry_t *entry;
	ivalue_t kk;

	_idict_refval(&kk, key);
	entry = _idict_search(dict, &kk);

	if (entry == NULL) 
		return -1;

	return _idict_del(dict, entry);
}

/* update (key, val) from dict */
ilong idict_update(idict_t *dict, const ivalue_t *key, const ivalue_t *val)
{
	ivalue_t kk;

	_idict_refval(&kk, key);

	return _idict_update(dict, &kk, val, 1);
}

/* pick entry from pos */
static inline idictentry_t *_idict_pick(const idict_t *dict, ilong pos)
{
	idictentry_t *entry;
	ib_memnode *nodes;

	nodes = (ib_memnode*)&dict->nodes;
	if (pos < 0 || pos >= nodes->node_max) return NULL;
	if (IMNODE_MODE(nodes, pos) == 0) return NULL;
	entry = (idictentry_t*)IMNODE_DATA(nodes, pos);

	return entry;
}

/* get key from pos */
ivalue_t *idict_pos_get_key(idict_t *dict, ilong pos)
{
	idictentry_t *entry;
	entry = _idict_pick(dict, pos);
	if (entry == NULL) return NULL;
	return &entry->key;
}

/* get value from pos */
ivalue_t *idict_pos_get_val(idict_t *dict, ilong pos)
{
	idictentry_t *entry;
	entry = _idict_pick(dict, pos);
	if (entry == NULL) return NULL;
	return &entry->val;
}

/* get sid from pos */
ilong idict_pos_get_sid(idict_t *dict, ilong pos)
{
	idictentry_t *entry;
	entry = _idict_pick(dict, pos);
	if (entry == NULL) return -1;
	return entry->sid;
}

/* update from pos */
void idict_pos_update(idict_t *dict, ilong pos, const ivalue_t *val)
{
	idictentry_t *entry;
	entry = _idict_pick(dict, pos);
	if (entry == NULL) return;
	it_cpy(&entry->val, val);
}

/* delete */
void idict_pos_delete(idict_t *dict, ilong pos)
{
	idictentry_t *entry;
	entry = _idict_pick(dict, pos);
	if (entry == NULL) return;
	_idict_del(dict, entry);
}

/* get first pos */
ilong idict_pos_head(idict_t *dict)
{
	return imnode_head(&dict->nodes);
}

/* get next pos */
ilong idict_pos_next(idict_t *dict, ilong pos)
{
	return imnode_next(&dict->nodes, pos);
}

/* clear dict */
void idict_clear(idict_t *dict)
{
	assert(dict);
	while (1) {
		ilong pos = idict_pos_head(dict);
		if (pos < 0) break;
		idict_pos_delete(dict, pos);
	}
}


/*
 * directly typing interface 
 */

/* search: key(str) val(str) */
int idict_search_ss(idict_t *dict, const char *key, ilong keysize,
	char **val, ilong *valsize)
{
	ivalue_t kk, *vv;
	it_strref(&kk, key, keysize);
	vv = idict_search(dict, &kk, 0);
	if (valsize) valsize[0] = -1;
	if (vv == NULL) return -1;
	if (it_type(vv) != ITYPE_STR) return 1;
	if (val) val[0] = it_str(vv);
	if (valsize) valsize[0] = it_size(vv);
	return 0;
}

/* search: key(int) val(str) */
int idict_search_is(idict_t *dict, ilong key, char **val, ilong *valsize)
{
	ivalue_t kk, *vv;
	it_init_int(&kk, key);
	vv = idict_search(dict, &kk, 0);
	if (valsize) valsize[0] = -1;
	if (vv == NULL) return -1;
	if (it_type(vv) != ITYPE_STR) return 1;
	if (val) val[0] = it_str(vv);
	if (valsize) valsize[0] = it_size(vv);
	return 0;
}

/* search: key(str) val(int) */
int idict_search_si(idict_t *dict, const char *key, ilong keysize, 
	ilong *val)
{
	ivalue_t kk, *vv;
	it_strref(&kk, key, keysize);
	vv = idict_search(dict, &kk, 0);
	if (vv == NULL) return -1;
	if (it_type(vv) != ITYPE_INT) return 1;
	if (val) val[0] = it_int(vv);
	return 0;
}

/* search: key(int) val(int) */
int idict_search_ii(idict_t *dict, ilong key, ilong *val)
{
	ivalue_t kk, *vv;
	it_init_int(&kk, key);
	vv = idict_search(dict, &kk, 0);
	if (vv == NULL) return -1;
	if (it_type(vv) != ITYPE_INT) return 1;
	if (val) val[0] = it_int(vv);
	return 0;
}

/* search: key(str) val(ptr) */
int idict_search_sp(idict_t *dict, const char *key, ilong keysize, void**ptr)
{
	ivalue_t kk, *vv;
	it_strref(&kk, key, keysize);
	vv = idict_search(dict, &kk, 0);
	if (ptr) ptr[0] = NULL;
	if (vv == NULL) return -1;
	if (it_type(vv) != ITYPE_PTR) return 1;
	if (ptr) ptr[0] = it_ptr(vv);
	return 0;
}

/* search: key(int) val(ptr) */
int idict_search_ip(idict_t *dict, ilong key, void**ptr)
{
	ivalue_t kk, *vv;
	it_init_int(&kk, key);
	vv = idict_search(dict, &kk, 0);
	if (ptr) ptr[0] = NULL;
	if (vv == NULL) return -1;
	if (it_type(vv) != ITYPE_PTR) return 1;
	if (ptr) ptr[0] = it_ptr(vv);
	return 0;
}

/* add: key(str) val(str) */
ilong idict_add_ss(idict_t *dict, const char *key, ilong keysize,
	const char *val, ilong valsize)
{
	ivalue_t kk, vv;
	it_strref(&kk, key, keysize);
	it_strref(&vv, val, valsize);
	return idict_add(dict, &kk, &vv);
}

/* add: key(int) val(str) */
ilong idict_add_is(idict_t *dict, ilong key, const char *val, ilong valsize)
{
	ivalue_t kk, vv;
	it_init_int(&kk, key);
	it_strref(&vv, val, valsize);
	return idict_add(dict, &kk, &vv);
}

/* add: key(str) val(int) */
ilong idict_add_si(idict_t *dict, const char *key, ilong keysize, ilong val)
{
	ivalue_t kk, vv;
	it_strref(&kk, key, keysize);
	it_init_int(&vv, val);
	return idict_add(dict, &kk, &vv);
}

/* add: key(int) val(int) */
ilong idict_add_ii(idict_t *dict, ilong key, ilong val)
{
	ivalue_t kk, vv;
	it_init_int(&kk, key);
	it_init_int(&vv, val);
	return idict_add(dict, &kk, &vv);
}

/* add: key(str) val(ptr) */
ilong idict_add_sp(idict_t *dict, const char *key, ilong keysize, 
	const void *ptr)
{
	ivalue_t kk, vv;
	it_strref(&kk, key, keysize);
	it_init_ptr(&vv, ptr);
	return idict_add(dict, &kk, &vv);
}

/* add: key(int) val(ptr) */
ilong idict_add_ip(idict_t *dict, ilong key, const void *ptr)
{
	ivalue_t kk, vv;
	it_init_int(&kk, key);
	it_init_ptr(&vv, ptr);
	return idict_add(dict, &kk, &vv);
}

/* update: key(str) val(str) */
ilong idict_update_ss(idict_t *dict, const char *key, ilong keysize,
	const char *val, ilong valsize)
{
	ivalue_t kk, vv;
	it_strref(&kk, key, keysize);
	it_strref(&vv, val, valsize);
	return idict_update(dict, &kk, &vv);
}

/* update: key(int) val(str) */
ilong idict_update_is(idict_t *dict, ilong key, const char *val, 
	ilong valsize)
{
	ivalue_t kk, vv;
	it_init_int(&kk, key);
	it_strref(&vv, val, valsize);
	return idict_update(dict, &kk, &vv);
}

/* update: key(str) val(int) */
ilong idict_update_si(idict_t *dict, const char *key, ilong keysize,
	ilong val)
{
	ivalue_t kk, vv;
	it_strref(&kk, key, keysize);
	it_init_int(&vv, val);
	return idict_update(dict, &kk, &vv);
}

/* update: key(int) val(int) */
ilong idict_update_ii(idict_t *dict, ilong key, ilong val)
{
	ivalue_t kk, vv;
	it_init_int(&kk, key);
	it_init_int(&vv, val);
	return idict_update(dict, &kk, &vv);
}

/* update: key(str) val(ptr) */
ilong idict_update_sp(idict_t *dict, const char *key, ilong keysize, 
	const void *ptr)
{
	ivalue_t kk, vv;
	it_strref(&kk, key, keysize);
	it_init_ptr(&vv, ptr);
	return idict_update(dict, &kk, &vv);
}

/* update: key(int) val(ptr) */
ilong idict_update_ip(idict_t *dict, ilong key, const void *ptr)
{
	ivalue_t kk, vv;
	it_init_int(&kk, key);
	it_init_ptr(&vv, ptr);
	return idict_update(dict, &kk, &vv);
}

/* delete: key(str) */
int idict_del_s(idict_t *dict, const char *key, ilong keysize)
{
	ivalue_t kk;
	it_strref(&kk, key, keysize);
	return idict_del(dict, &kk);
}

/* delete: key(int) */
int idict_del_i(idict_t *dict, ilong key)
{
	ivalue_t kk;
	it_init_int(&kk, key);
	return idict_del(dict, &kk);
}



/**********************************************************************
 * ivalue_t string library
 **********************************************************************/

/* get sub str */
ivalue_t *it_strsub(const ivalue_t *src, ivalue_t *dst, ilong start, 
	ilong endup)
{
	ilong size;

	if (dst == NULL) return dst;
	if (src == NULL) {
		it_sresize(dst, 0);
		return dst;
	}

	assert(it_type(src) == ITYPE_STR);
	assert(it_type(dst) == ITYPE_STR);

	if (start < 0) start = it_size(src) + start;
	if (start < 0) start = 0;
	if (endup < 0) endup = it_size(src) + endup;
	if (endup < 0) endup = 0;
	if (endup > (ilong)it_size(src)) endup = (ilong)it_size(src);

	if (start >= endup) {
		it_sresize(dst, 0);
	}	else {
		size = (ilong)(endup - start);
		it_sresize(dst, size);
		memcpy(it_str(dst), it_str(src) + start, size);
	}

	return dst;
}

/* str compare */
static int it_strcmpx(const ivalue_t *src, const ivalue_t *str, ilong start, 
	int incase)
{
	iulong minsize, size, i;
	const char *p1, *p2;
	int retval;

	assert(it_type(src) == ITYPE_STR);
	assert(it_type(str) == ITYPE_STR);

	if (start < 0) start = (ilong)it_size(src) + start;
	if (start < 0) start = 0;
	if (start > (ilong)it_size(src)) start = (ilong)it_size(src);

	size = it_size(src) - start;
	minsize = (IUINT32)_imin((IUINT32)size, (IUINT32)it_size(str));

	p1 = it_str(src) + start;
	p2 = it_str(str);
	retval = 0;
	
	if (incase == 0) {
		for (i = minsize; i > 0; i--, p1++, p2++) {
			if (*p1 > *p2) { retval = 1; break; }
			else if (*p1 < *p2) { retval = -1; break; }
		}
	}	else {
		char c1, c2;
		for (i = minsize; i > 0; i--, p1++, p2++) {
			c1 = ITOUPPER(*p1);
			c2 = ITOUPPER(*p2);
			if (c1 > c2) { retval = 1; break; }
			else if (c1 < c2) { retval = -1; break; }
		}
	}

	if (retval != 0 || size == it_size(str)) 
		return retval;

	if (size > it_size(str)) return 1;

	return -1;
}

/* str compare */
int it_strcmp(const ivalue_t *src, const ivalue_t *str, ilong start)
{
	return it_strcmpx(src, str, start, 0);
}

/* str compare case insensitive */
int it_stricmp(const ivalue_t *src, const ivalue_t *str, ilong start)
{
	return it_strcmpx(src, str, start, 1);
}

/* str compare with c str */
int it_strcmpc(const ivalue_t *src, const char *str, ilong start)
{
	ivalue_t tt;
	it_strref(&tt, str, (ilong)strlen(str));
	return it_strcmpx(src, &tt, start, 0);
}

/* str compare with c string (case insensitive) */
int it_stricmpc(const ivalue_t *src, const char *str, ilong start)
{
	ivalue_t tt;
	it_strref(&tt, str, (ilong)strlen(str));
	return it_strcmpx(src, &tt, start, 1);
}

/* it_strsep */
int it_strsep(const ivalue_t *src, iulong *pos, ivalue_t *dst,
	const ivalue_t *sep)
{
	iulong current, size, endup, i, s1, s2;
	const char *p1, *p2;

	if (src == NULL || dst == NULL) return -1;
	if (it_type(src) != ITYPE_STR || it_type(dst) != ITYPE_STR) return -2;

	p1 = it_str(src);

	current = (pos != NULL)? *pos : 0;

	if (current > it_size(src)) {
		it_sresize(dst, 0);
		return -3;
	}

	if (sep == NULL || pos == NULL) {
		size = it_size(src) - current;
		it_sresize(dst, size);
		memcpy(it_str(dst), it_str(src) + current, size);
		if (pos) *pos = it_size(src);
		return 0;
	}

	p2 = it_str(sep);

	s1 = it_size(src);
	s2 = it_size(sep);

	for (endup = current; endup < s1; endup++) {
		for (i = 0; i < s2; i++) {
			if (p1[endup] == p2[i]) break;
		}
		if (i < s2) break;
	}

	size = endup - current;
	if (pos) *pos = (long)endup + 1;

	it_sresize(dst, size);
	memcpy(it_str(dst), it_str(src) + current, size);

	return 0;
}

/* it_strsepc */
int it_strsepc(const ivalue_t *src, iulong *pos, ivalue_t *dst,
	const char *sep)
{
	ivalue_t vsep;
	assert(it_type(src) == ITYPE_STR);
	assert(it_type(dst) == ITYPE_STR);
	it_strref(&vsep, sep, (ilong)strlen(sep));
	return it_strsep(src, pos, dst, &vsep);
}

/* it_strstrip */
ivalue_t *it_strstrip(ivalue_t *str, const ivalue_t *delim)
{
	iulong size, dlen, i, j, k;
	const char *span;
	char *ptr;

	if (it_type(str) != ITYPE_STR || it_type(delim) != ITYPE_STR) 
		return str;

	ptr = it_str(str);
	size = it_size(str);
	dlen = it_size(delim);
	span = it_str(delim);

	while (size > 0) {
		for (j = 0; j < dlen; j++) {
			if (span[j] == ptr[size - 1]) break;
		}
		if (j >= dlen) break;
		size--;
	}

	ptr[size] = 0;
	it_size(str) = size;

	for (i = 0, k = 0; i < size; i++) {
		for (j = 0; j < dlen; j++) 
			if (span[j] == ptr[i]) break;
		if (j >= dlen) break;
		k++;
	}

	if (k > 0) {
		for (i = 0; i + k < size; i++) ptr[i] = ptr[i + k];
		ptr[i] = 0;
		size = size - k;
	}

	it_sresize(str, size);

	return str;
}

/* ivalue_t str strip */
ivalue_t *it_strstripc(ivalue_t *str, const char *delim)
{
	ivalue_t d;
	it_strref(&d, delim, (ilong)strlen(delim));
	return it_strstrip(str, &d);
}

/* find str in src */
static ilong it_strfindx(const ivalue_t *src, const ivalue_t *str,
	ilong start, ilong endup, int incase, int reverse)
{
	const char *p1 = it_str(src);
	const char *p2 = it_str(str);
	const char *pend, *p;
	iulong size, i;

	assert(it_type(src) == ITYPE_STR);
	assert(it_type(str) == ITYPE_STR);

	if (start < 0) start = (ilong)it_size(src) + start;
	if (start < 0) start = 0;
	if (endup < 0) endup = (ilong)it_size(src) + endup;
	if (endup < 0) endup = 0;
	if (endup > (ilong)it_size(src)) endup = (ilong)it_size(src);

	size = it_size(str);
	pend = p1 + endup;

	if (start + size > it_size(src) || start >= endup) 
		return -1;

	if (reverse == 0) {
		if (incase == 0) {
			for (p = p1 + start; p + size <= pend; p++) {
				if (memcmp(p, p2, size) == 0) 
					return (ilong)(p - p1);
			}
		}	else {
			for (p = p1 + start; p + size <= pend; p++) {
				for (i = 0; i < size; i++) {
					if (ITOUPPER(p[i]) != ITOUPPER(p2[i])) break;
				}
				if (i >= size) 
					return (ilong)(p - p1);
			}
		}
	}	
	else {
		if (incase == 0) {
			for (p = pend - size; p >= p1 + start; p--) {
				if (memcmp(p, p2, size) == 0) 
					return (ilong)(p - p1);
			}
		}	else {
			for (p = pend - size; p >= p1 + start; p--) {
				for (i = 0; i < size; i++) {
					if (ITOUPPER(p[i]) != ITOUPPER(p2[i])) break;
				}
				if (i >= size) 
					return (ilong)(p - p1);
			}
		}
	}

	return -1;
}

/* find str in src (s:start, e:endup) */
ilong it_strfind(const ivalue_t *src, const ivalue_t *str, ilong s, ilong e)
{
	return it_strfindx(src, str, s, e, 0, 0);
}

/* find str in src */
ilong it_strfind2(const ivalue_t *src, const ivalue_t *str, ilong start)
{
	return it_strfindx(src, str, start, it_size(src), 0, 0);
}

/* find str in src (s:start, e:endup) case insensitive */
ilong it_strfindi(const ivalue_t *src, const ivalue_t *str, ilong s, ilong e)
{
	return it_strfindx(src, str, s, e, 1, 0);
}

/* find str in src */
ilong it_strfindi2(const ivalue_t *src, const ivalue_t *str, ilong start)
{
	return it_strfindx(src, str, start, it_size(src), 1, 0);
}

/* c: find str in src */
ilong it_strfindc(const ivalue_t *src, const char *str, ilong s, ilong e)
{
	ivalue_t tt;
	it_strref(&tt, str, (long)strlen(str));
	return it_strfindx(src, &tt, s, e, 0, 0);
}

/* c: find str in src */
ilong it_strfindc2(const ivalue_t *src, const char *str, ilong start)
{
	return it_strfindc(src, str, start, it_size(src));
}

/* c: find str in src */
ilong it_strfindic(const ivalue_t *src, const char *str, ilong s, ilong e)
{
	ivalue_t tt;
	it_strref(&tt, str, (ilong)strlen(str));
	return it_strfindx(src, &tt, s, e, 1, 0);
}

/* c: find str in src */
ilong it_strfindic2(const ivalue_t *src, const char *str, ilong start)
{
	return it_strfindic(src, str, start, it_size(src));
}

/* find from right */
ilong it_strfindr(const ivalue_t *src, const ivalue_t *str, ilong s, ilong e)
{
	return it_strfindx(src, str, s, e, 0, 1);
}

/* find from right */
ilong it_strfindri(const ivalue_t *src, const ivalue_t *str, 
	ilong s, ilong e)
{
	return it_strfindx(src, str, s, e, 1, 1);
}

/* case change: change=0: upper, change=1: lowwer */
ivalue_t *it_strcase(ivalue_t *src, int change)
{
	char *p, *pe;
	assert(it_type(src) == ITYPE_STR);
	p = it_str(src);
	pe = it_str(src) + it_size(src);
	if (change == 0) {
		for (; p < pe; p++) {
			if (*p >= 'a' && *p <= 'z') *p -= 'a' - 'A';
		}
	}	else {
		for (; p < pe; p++) {
			if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
		}
	}
	return src;
}

/* append integer */
ivalue_t *it_strappendl(ivalue_t *src, ilong val, int radix)
{
	char digit[32];
	assert(it_type(src) == ITYPE_STR);
	iltoa((long)val, digit, radix);
	it_strcatc2(src, digit);
	return src;
}

/* append unsigned int */
ivalue_t *it_strappendul(ivalue_t *src, iulong val, int radix)
{
	char digit[32];
	assert(it_type(src) == ITYPE_STR);
	iultoa((unsigned long)val, digit, radix);
	it_strcatc2(src, digit);
	return src;
}

/* set integer */
ivalue_t *it_strsetl(ivalue_t *src, ilong val, int radix)
{
	assert(it_type(src) == ITYPE_STR);
	it_sresize(src, 0);
	return it_strappendl(src, val, radix);
}

/* set unsigned int */
ivalue_t *it_strsetul(ivalue_t *src, iulong val, int radix)
{
	assert(it_type(src) == ITYPE_STR);
	it_sresize(src, 0);
	return it_strappendul(src, val, radix);
}

/* left just */
ivalue_t *it_strljust(ivalue_t *src, iulong width, char fill)
{
	iulong size = it_size(src);
	assert(it_type(src) == ITYPE_STR);
	if (size < width) {
		it_sresize(src, width);
		memset(it_str(src) + size, fill, width - size);
	}
	return src;
}

/* right just */
ivalue_t *it_strrjust(ivalue_t *src, iulong width, char fill)
{
	iulong size = it_size(src);
	assert(it_type(src) == ITYPE_STR);
	if (size < width) {
		it_sresize(src, width);
		memmove(it_str(src) + width - size, it_str(src), size);
		memset(it_str(src), fill, width - size);
	}
	return src;
}

/* middle just */
ivalue_t *it_strmiddle(ivalue_t *src, iulong width, char fill)
{
	iulong size = it_size(src);
	assert(it_type(src) == ITYPE_STR);
	if (size < width) {
		it_strljust(src, size + (width - size) / 2, fill);
		it_strrjust(src, width, fill);
	}
	return src;
}

/* replace: if count >= 0 only the first count occurrences are replaced */
ivalue_t *it_replace(const ivalue_t *src, ivalue_t *out,
	const ivalue_t *str_old, const ivalue_t *str_new, ilong count)
{
	const char *ptr_old = it_str(str_old);
	const char *ptr_new = it_str(str_new);
	const char *ptr_src;
	ilong size_old = it_size(str_old);
	ilong size_new = it_size(str_new);
	ilong position = 0, i = 0;
	ivalue_t saved;

	if (it_type(src) != ITYPE_STR || it_type(out) != ITYPE_STR ||
		it_type(str_old) != ITYPE_STR || it_type(str_new) != ITYPE_STR) {
		return NULL;
	}

	if (count == 0) {
		if (src != out) it_cpy(out, src);
		return out;
	}

	if (size_old == 1 && size_new == 1) {
		char chold = ptr_old[0];
		char chnew = ptr_new[0];
		char *ptr_out;
		if (src != out) it_cpy(out, src);
		ptr_out = it_str(out);
		if (count < 0) {
			for (i = it_size(out); i > 0; ptr_out++, i--) {
				if (ptr_out[0] == chold) ptr_out[0] = chnew;
			}
		}	else {
			for (i = it_size(out); i > 0; ptr_out++, i--) {
				if (ptr_out[0] == chold) {
					ptr_out[0] = chnew;
					if (--count <= 0) break;
				}
			}
		}
		return out;
	}

	if (src == out) {
		it_init(&saved, ITYPE_STR);
		it_cpy(&saved, src);
		src = &saved;
	}

	ptr_src = it_str(src);
	it_sresize(out, 0);

	while (1) {
		ilong retval = it_strfind2(src, str_old, position);
		if (retval < 0) break;
		if (retval > position) {
			it_strcatc(out, ptr_src + position, retval - position);
		}
		if (size_new > 0) {
			it_strcatc(out, ptr_new, size_new);
		}
		position = retval + size_old;
		if (count > 0) {
			if (--count <= 0) break;
		}
	}

	if (position < (ilong)it_size(src)) 
		it_strcatc(out, ptr_src + position, (ilong)it_size(src) - position);

	if (src == &saved) 
		it_destroy(&saved);

	return out;
}


/**********************************************************************
 * string list
 **********************************************************************/
/* create string list */
istring_list_t* istring_list_new(void)
{
	istring_list_t *strings;

	strings = (istring_list_t*)ikmem_malloc(sizeof(istring_list_t));
	if (strings == NULL) return NULL;

	strings->vector = iv_create();

	if (strings->vector == NULL) {
		ikmem_free(strings);
		return NULL;
	}

	strings->values = NULL;
	strings->count = 0;

	it_init(&strings->none, ITYPE_NONE);

	return strings;
}

/* delete string list */
void istring_list_delete(istring_list_t *strings)
{
	if (strings) {
		if (strings->values) {
			ilong i;
			for (i = strings->count - 1; i >= 0; i--) {
				it_destroy(strings->values[i]);
				ikmem_free(strings->values[i]);
			}
			strings->values = NULL;
		}
		if (strings->vector) {
			iv_delete(strings->vector);
			strings->vector = NULL;
		}
		strings->count = 0;
		ikmem_free(strings);
	}
}

/* insert at pos */
int istring_list_insert(istring_list_t *strings, ilong pos, 
	const ivalue_t *value)
{
	ivalue_t **values;
	ilong newsize, i;
	if (pos < 0) pos = strings->count + pos + 1;
	if (pos < 0) pos = 0;
	if (pos > strings->count) {
		/* sparse insert: fill gaps with NONE values */
		newsize = pos + 1;
		if (iv_resize(strings->vector, newsize * sizeof(void*)) != 0)
			return -1;
		strings->values = (ivalue_t**)strings->vector->data;
		values = strings->values;
		for (i = strings->count; i < newsize; i++) 
			values[i] = NULL;
		for (i = strings->count; i < pos; i++) {
			values[i] = (ivalue_t*)ikmem_malloc(sizeof(ivalue_t));
			if (values[i] == NULL) return -2;
			it_init(values[i], ITYPE_NONE);
		}
		strings->count = newsize;
	}	else {
		/* insert within range: grow by 1 and shift right */
		newsize = strings->count + 1;
		if (iv_resize(strings->vector, newsize * sizeof(void*)) != 0)
			return -1;
		strings->values = (ivalue_t**)strings->vector->data;
		values = strings->values;
		for (i = strings->count; i > pos; i--) 
			values[i] = values[i - 1];
		strings->count = newsize;
	}

	values[pos] = (ivalue_t*)ikmem_malloc(sizeof(ivalue_t));
	if (values[pos] == NULL) return -3;

	it_init(values[pos], ITYPE_NONE);
	it_cpy(values[pos], value);

	return 0;
}

/* remove at pos */
void istring_list_remove(istring_list_t *strings, ilong pos)
{
	ivalue_t **values = strings->values;
	ilong i;
	if (pos < 0) pos = strings->count + pos;
	if (pos < 0 || pos >= strings->count) return;
	if (values[pos]) {
		it_destroy(values[pos]);
		ikmem_free(values[pos]);
		values[pos] = NULL;
	}
	for (i = pos; i < strings->count - 1; i++) 
		values[i] = values[i + 1];
	strings->count--;
}

/* clear list */
void istring_list_clear(istring_list_t *strings)
{
	ivalue_t **values = strings->values;
	ilong i;
	for (i = 0; i < strings->count; i++) {
		if (values[i] != NULL) {
			it_destroy(values[i]);
			ikmem_free(values[i]);
			values[i] = NULL;
		}
	}
	strings->count = 0;
}

/* insert at pos */
int istring_list_insertc(istring_list_t *strings, ilong pos, 
	const char *value, ilong size)
{
	ivalue_t tt;
	if (size < 0) size = strlen(value);
	it_strref(&tt, value, (long)size);
	return istring_list_insert(strings, pos, &tt);
}

/* push back */
int istring_list_push_back(istring_list_t *strings, const ivalue_t *value)
{
	return istring_list_insert(strings, -1, value);
}

/* push back */
int istring_list_push_backc(istring_list_t *strings, const char *value, 
	ilong size)
{
	ivalue_t tt;
	if (size < 0) size = strlen(value);
	it_strref(&tt, value, size);
	return istring_list_insert(strings, -1, &tt);
}

/* encode into csv row */
int istring_list_csv_encode(const istring_list_t *strings, ivalue_t *csvrow)
{
	ilong total, size, i, j;
	char *ptr;

	for (i = total = 0; i < strings->count; i++) {
		const ivalue_t *src = strings->values[i];
		total += istrsave(it_str(src), src->size, NULL);
		total += 3;
	}

	it_sresize(csvrow, total);
	ptr = it_str(csvrow);

	for (i = total = 0; i < strings->count; i++) {
		const ivalue_t *src = strings->values[i];
		const char *ss = (const char*)it_str(src);
		int escape = 0;
		size = it_size(src);
		for (j = 0; j < size; j++) {
			if (ss[j] == '"' || ss[j] == ',' || ss[j] == '\0') {
				escape = 1;
				break;
			}
		}
		if (escape) *ptr++ = '"';
		size = istrsave(ss, size, ptr);
		ptr += size;
		if (escape) *ptr++ = '"';
		if (i < strings->count - 1) *ptr++ = ',';
	}

	ptr[0] = 0;
	it_sresize(csvrow, ptr - it_str(csvrow));

	return 0;
}

/* decode from csv row */
istring_list_t *istring_list_csv_decode(const char *csvrow, ilong size)
{
	istring_list_t *strings = NULL;
	ivalue_t source, newstr;
	ilong ilen, inext;

	if (size < 0) size = strlen(csvrow);

	for (; size > 0; size--) {
		if (csvrow[size - 1] != '\n') break;
	}

	strings = istring_list_new();
	if (strings == NULL) return NULL;

	it_init(&source, ITYPE_STR);
	it_init(&newstr, ITYPE_STR);

	it_strcpyc(&source, csvrow, size);

	for (inext = 0, ilen = 0; ; ) {
		const char *ptr = istrcsvtok(it_str(&source), &inext, &ilen);
		if (ptr == NULL) break;
		if (ptr[0] == '"' && ilen > 1) {
			if (ptr[ilen - 1] == '"') ptr++, ilen -= 2;
		}
		it_sresize(&newstr, ilen);
		ilen = istrload(ptr, ilen, it_str(&newstr));
		it_sresize(&newstr, ilen);
		istring_list_push_back(strings, &newstr);
	}

	it_destroy(&newstr);
	it_destroy(&source);

	return strings;
}

/* split str */
istring_list_t *istring_list_split(const char *text, ilong len,
	const char *seps, ilong seplen)
{
	ivalue_t src, sep, value;
	istring_list_t *strings;
	iulong next = 0;
	it_strref(&src, text, len);
	it_strref(&sep, seps, seplen);
	strings = istring_list_new();
	if (strings == NULL) return NULL;
	it_init(&value, ITYPE_STR);
	while (1) {
		if (it_strsep(&src, &next, &value, &sep) != 0) break;
		istring_list_push_back(strings, &value);
	}
	it_destroy(&value);
	return strings;
}

/* join str list */
int istring_list_join(const istring_list_t *strings, const char *str, 
	ilong size, ivalue_t *output) 
{
	ilong needed, count, i;
	char *ptr;
	if (size < 0) size = (ilong)strlen(str);
	count = (ilong)strings->count;
	for (i = count - 1, needed = 0; i >= 0; i--) {
		needed += it_size(strings->values[i]);
		if (i > 0) needed += size;
	}
	it_sresize(output, needed);
	ptr = it_str(output);
	for (i = 0; i < count; i++) {
		const ivalue_t *string = strings->values[i];
		ilong length = it_size(string);
		memcpy(ptr, it_str(string), length);
		ptr += length;
		if (i < count - 1) {
			memcpy(ptr, str, size);
			ptr += size;
		}
	}
	ptr[0] = 0;
	return 0;
}




