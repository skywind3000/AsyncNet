/**********************************************************************
 *
 * imemdata.h - basic data structures and algorithms
 *
 * VALUE INTERFACE:
 * type independance value classes
 *
 * DICTIONARY INTERFACE:
 * feature: 2.1-2.2 times faster than std::map (string or integer key)
 * feature: 1.3-1.5 times faster than stdext::hash_map 
 *
 * for more information, please see the readme file
 *
 **********************************************************************/

#ifndef __IMEMDATA_H__
#define __IMEMDATA_H__

#include "imembase.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>


/**********************************************************************
 * 32BIT INTEGER DEFINITION 
 **********************************************************************/
#ifndef __INTEGER_32_BITS__
#define __INTEGER_32_BITS__
#if defined(__UINT32_TYPE__) && defined(__UINT32_TYPE__)
	typedef __UINT32_TYPE__ ISTDUINT32;
	typedef __INT32_TYPE__ ISTDINT32;
#elif defined(__UINT_FAST32_TYPE__) && defined(__INT_FAST32_TYPE__)
	typedef __UINT_FAST32_TYPE__ ISTDUINT32;
	typedef __INT_FAST32_TYPE__ ISTDINT32;
#elif defined(_WIN64) || defined(WIN64) || defined(__amd64__) || \
	defined(__x86_64) || defined(__x86_64__) || defined(_M_IA64) || \
	defined(_M_AMD64)
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#elif defined(_WIN32) || defined(WIN32) || defined(__i386__) || \
	defined(__i386) || defined(_M_X86)
	typedef unsigned long ISTDUINT32;
	typedef long ISTDINT32;
#elif defined(__MACOS__)
	typedef UInt32 ISTDUINT32;
	typedef SInt32 ISTDINT32;
#elif defined(__APPLE__) && defined(__MACH__)
	#include <sys/types.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif defined(__BEOS__)
	#include <sys/inttypes.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif (defined(_MSC_VER) || defined(__BORLANDC__)) && (!defined(__MSDOS__))
	typedef unsigned __int32 ISTDUINT32;
	typedef __int32 ISTDINT32;
#elif defined(__GNUC__) && (__GNUC__ > 3)
	#include <stdint.h>
	typedef uint32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#else 
	typedef unsigned long ISTDUINT32; 
	typedef long ISTDINT32;
#endif
#endif


/**********************************************************************
 * Global Macros
 **********************************************************************/
#ifndef __IINT8_DEFINED
#define __IINT8_DEFINED
typedef char IINT8;
#endif

#ifndef __IUINT8_DEFINED
#define __IUINT8_DEFINED
typedef unsigned char IUINT8;
#endif

#ifndef __IUINT16_DEFINED
#define __IUINT16_DEFINED
typedef unsigned short IUINT16;
#endif

#ifndef __IINT16_DEFINED
#define __IINT16_DEFINED
typedef short IINT16;
#endif

#ifndef __IINT32_DEFINED
#define __IINT32_DEFINED
typedef ISTDINT32 IINT32;
#endif

#ifndef __IUINT32_DEFINED
#define __IUINT32_DEFINED
typedef ISTDUINT32 IUINT32;
#endif

#ifndef __IINT64_DEFINED
#define __IINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64 IINT64;
#else
typedef long long IINT64;
#endif
#endif

#ifndef __IUINT64_DEFINED
#define __IUINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef unsigned __int64 IUINT64;
#else
typedef unsigned long long IUINT64;
#endif
#endif

#ifndef INLINE
#if defined(__GNUC__)

#if (__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1))
#define INLINE         __inline__ __attribute__((always_inline))
#else
#define INLINE         __inline__
#endif

#elif (defined(_MSC_VER) || defined(__BORLANDC__) || defined(__WATCOMC__))
#define INLINE __inline
#else
#define INLINE 
#endif
#endif

#ifndef inline
#define inline INLINE
#endif


/**********************************************************************
 * DETECTION WORD ORDER
 **********************************************************************/
#ifndef IWORDS_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MISPEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #define IWORDS_BIG_ENDIAN  0
    #endif
#endif

#ifndef IASSERT
#define IASSERT(x) assert(x)
#endif



#ifdef __cplusplus
extern "C" {
#endif


/**********************************************************************
 * VALUE OPERATION:
 *
 * auto-type value operation. there are 6 basic value types:
 *
 * ITYPE_NONE    - for none
 * ITYPE_INT     - for a integer type
 * ITYPE_FLOAT   - for a float type
 * ITYPE_STR     - for a string type (ptr + size)
 * ITPYE_PTR     - for a pointer
 * ITYPE_EXTRA   - for a extra value
 *
 * support value operation: cmp, add, sub, mul ... 
 *
 **********************************************************************/
#define IFLOATTYPE	float

typedef union { void *p; ilong l; int i; char c; IFLOATTYPE f; } ITYPEUNION;

/*-------------------------------------------------------------------*/
/* IVALUE - value definition                                         */
/*-------------------------------------------------------------------*/
struct IVALUE
{
	ITYPEUNION tu;
	short type;
	short rehash;
	iulong hash;
	iulong size;
	ilong ref;
	ilong param;
};

typedef struct IVALUE ivalue_t;

#define it_ptr(v)	((v)->tu.p)
#define it_int(v)	((v)->tu.l)
#define it_chr(v)	((v)->tu.c)
#define it_flt(v)	((v)->tu.f)
#define it_str(v)	((char*)((v)->tu.p))

#define it_type(v)	((v)->type)
#define it_hash(v)	((v)->hash)
#define it_size(v)	((v)->size)
#define it_ref(v)	((v)->ref)
#define it_rehash(v)	((v)->rehash)

#define ITYPE_NONE		0
#define ITYPE_INT		1
#define ITYPE_FLOAT		2
#define ITYPE_STR		3
#define ITYPE_PTR		4
#define ITYPE_EXTRA		5


/* init value */
static inline void it_init(ivalue_t *v, int tt)
{
	v->type = tt; 
	v->hash = 0; 
	v->rehash = 0; 
	v->size = 0; 
	v->ref = 0; 
	v->param = 0;	
	switch (tt) { 
	case ITYPE_INT: it_int(v) = 0; break; 
	case ITYPE_FLOAT: it_flt(v) = (IFLOATTYPE)0; break; 
	case ITYPE_STR: it_ptr(v) = &v->param; break; 
	case ITYPE_PTR: it_ptr(v) = NULL; break; 
	default: it_ptr(v) = NULL; break; 
	}
}

/* destroy value */
static inline void it_destroy(ivalue_t *v)
{
	if (it_type(v) == ITYPE_STR) { 
		if (it_ptr(v) != &(v->param)) 
			ikmem_free(it_str(v)); 
	}	
	v->type = ITYPE_NONE; 
	v->size = 0; 
	it_ptr(v) = NULL; 
}

/* string value resize */
static inline void it_sresize(ivalue_t *v, iulong s)
{
	iulong newsize = s;
	iulong need = newsize + 1;
	iulong block = 0;
	if (it_ptr(v) == &(v->param)) { 
		if (need > sizeof(v->param)) {
			for (block = 1; block < need; block <<= 1);
			it_ptr(v) = ikmem_malloc(block); 
			assert(it_ptr(v));
			memcpy(it_ptr(v), &(v->param), it_size(v));
		}	
	}	else { 
		if (need > sizeof(v->param)) { 
			iulong oblock = ikmem_ptr_size(it_str(v));
			if (need > oblock || need <= (oblock >> 1)) {
				for (block = 1; block < need; block <<= 1);
				it_ptr(v) = ikmem_realloc(it_str(v), block); 
				assert(it_ptr(v));
			}
		}	else { 
			memcpy(&(v->param), it_ptr(v), newsize);
			ikmem_free(it_str(v)); 
			it_ptr(v) = &v->param; 
		}	
	}	
	it_str(v)[newsize] = 0; 
	it_size(v) = newsize; 
	v->rehash = 0; 
}

/* value copy */
static inline void it_cpy(ivalue_t *v1, const ivalue_t *v2)
{
	ilong ref = it_ref(v1);
	if (it_type(v1) == ITYPE_STR) { 
		if (it_type(v2) == ITYPE_STR) {	
			it_sresize(v1, it_size(v2)); 
			memcpy(it_str(v1), it_str(v2), it_size(v2));
		}	else {	
			it_destroy(v1);	
			*v1 = *v2;	
		}	
	}	else { 
		if (it_type(v2) == ITYPE_STR) {	
			it_destroy(v1);	
			it_init(v1, ITYPE_STR);	
			it_sresize(v1, it_size(v2));
			memcpy(it_str(v1), it_str(v2), it_size(v2));
		}	else {	
			*v1 = *v2; 
		}	
	}
	it_ref(v1) = ref;
}

/* value compare */
static inline int it_cmp(const ivalue_t *v1, const ivalue_t *v2) 
{
	ilong r = 0; 
	if (it_type(v1) != it_type(v2)) {		
		r = (int)it_type(v1) - (int)it_type(v2);	
	}	else { 
		switch (it_type(v1)) {	
		case ITYPE_NONE: break; 
		case ITYPE_INT: r = (it_int(v1) - it_int(v2)); break;
		case ITYPE_FLOAT:
			if (it_flt(v1) == it_flt(v2)) r = 0;
			else if (it_flt(v1) > it_flt(v2)) r = 1;
			else r = -1; 
			break; 
		case ITYPE_STR:	
			r = it_size(v1) < it_size(v2) ? it_size(v1) : it_size(v2); 
			r = memcmp(it_str(v1), it_str(v2), r); 
			if (r == 0) {
				if (it_size(v1) < it_size(v2)) r = -1; 
				else if (it_size(v1) > it_size(v2)) r = 1; 
			}	
			break;	
		case ITYPE_PTR:
			if (it_str(v1) == it_str(v2)) r = 0;
			else if (it_str(v1) > it_str(v2)) r = 1;
			else r = -1;
			break;
		}
	}
	return (int)r;
}

/* value add */
static inline int it_add(ivalue_t *dst, const ivalue_t *src)
{
	iulong savesize;
	int retval = 0;
	if (it_type(dst) == it_type(src)) {
		switch (it_type(dst)) {
		case ITYPE_INT: it_int(dst) += it_int(src); break;
		case ITYPE_FLOAT: it_flt(dst) += it_flt(src); break;
		case ITYPE_STR:
			savesize = it_size(dst);
			it_sresize(dst, savesize + it_size(src));
			memcpy(it_str(dst) + savesize, it_ptr(src), it_size(src));
			it_rehash(dst) = 0;
			break;
		default:
			retval = -1; 
			break;
		}
	}	else {
		switch (it_type(dst)) {
		case ITYPE_STR:
			if (it_type(src) == ITYPE_INT) {
				unsigned char *ptr;
				savesize = it_size(dst);
				it_sresize(dst, savesize + 1);
				ptr = (unsigned char*)it_str(dst) + savesize;
				ptr[0] = (unsigned char)(it_int(src) & 0xff);
				it_rehash(dst) = 0;
			}	else {
				retval = -2;
			}
			break;
		case ITYPE_FLOAT:
			if (it_type(src) == ITYPE_INT) {
				it_flt(dst) += (IFLOATTYPE)it_int(src);
			}	else {
				retval = -3;
			}
			break;
		case ITYPE_INT:
			if (it_type(src) == ITYPE_FLOAT) {
				it_int(dst) += (long)it_flt(src);
			}	else {
				retval = -4;
			}
			break;
		case ITYPE_PTR:
			if (it_type(src) == ITYPE_INT) {
				it_ptr(dst) = it_str(dst) + it_int(src);
			}	else {
				retval = -5;
			}
			break;
		default:
			retval = -6;
			break;
		}
	}
	return retval;
}

/* value sub */
static inline int it_sub(ivalue_t *dst, const ivalue_t *src)
{
	int retval = 0;
	if (it_type(dst) == it_type(src)) {
		switch (it_type(dst)) {
		case ITYPE_INT: it_int(dst) -= it_int(src); break;
		case ITYPE_FLOAT: it_flt(dst) -= it_flt(src); break;
		default:
			retval = -1; 
			break;
		}
	}	else {
		switch (it_type(dst)) {
		case ITYPE_FLOAT:
			if (it_type(src) == ITYPE_INT) {
				it_flt(dst) -= (IFLOATTYPE)it_int(src);
			}	else {
				retval = -3;
			}
			break;
		case ITYPE_INT:
			if (it_type(src) == ITYPE_FLOAT) {
				it_int(dst) -= (long)it_flt(src);
			}	else {
				retval = -4;
			}
			break;
		case ITYPE_PTR:
			if (it_type(src) == ITYPE_INT) {
				it_ptr(dst) = it_str(dst) - it_int(src);
			}	else {
				retval = -5;
			}
			break;
		default:
			retval = -6;
		}
	}
	return retval;
}

/* value mul */
static inline int it_mul(ivalue_t *dst, const ivalue_t *src)
{
	int retval = 0;
	if (it_type(dst) == it_type(src)) {
		switch (it_type(dst)) {
		case ITYPE_INT: it_int(dst) *= it_int(src); break;
		case ITYPE_FLOAT: it_flt(dst) *= it_flt(src); break;
		default:
			retval = -1; 
			break;
		}
	}	else {
		switch (it_type(dst)) {
		case ITYPE_STR:
			if (it_type(src) == ITYPE_INT) {
				iulong savesize, i;
				char *ptr;
				savesize = it_size(dst);
				it_sresize(dst, savesize * it_int(src));
				ptr = it_str(dst) + savesize;
				for (i = it_int(src) - 1; i > 0; i--) {
					memcpy(ptr, it_str(dst), savesize);
					ptr += savesize;
				}
				it_rehash(dst) = 0;
			}	else {
				retval = -2;
			}
			break;
		case ITYPE_FLOAT:
			if (it_type(src) == ITYPE_INT) {
				it_flt(dst) *= (IFLOATTYPE)it_int(src);
			}	else {
				retval = -3;
			}
			break;
		case ITYPE_INT:
			if (it_type(src) == ITYPE_FLOAT) {
				it_int(dst) *= (long)it_flt(src);
			}	else {
				retval = -4;
			}
			break;
		default:
			retval = -6;
		}
	}
	return retval;
}

/* value division */
static inline int it_div(ivalue_t *dst, const ivalue_t *src)
{
	int retval = 0;
	if (it_type(src) == ITYPE_INT && it_int(src) == 0) return 1;
	if (it_type(src) == ITYPE_FLOAT && it_flt(src) == 0.0f) return 1;
	if (it_type(dst) == it_type(src)) {
		switch (it_type(dst)) {
		case ITYPE_INT: it_int(dst) /= it_int(src); break;
		case ITYPE_FLOAT: it_flt(dst) /= it_flt(src); break;
		default:
			retval = -1; 
			break;
		}
	}	else {
		switch (it_type(dst)) {
		case ITYPE_FLOAT:
			if (it_type(src) == ITYPE_INT) {
				it_flt(dst) /= (IFLOATTYPE)it_int(src);
			}	else {
				retval = -3;
			}
			break;
		case ITYPE_INT:
			if (it_type(src) == ITYPE_FLOAT) {
				it_int(dst) /= (long)it_flt(src);
			}	else {
				retval = -4;
			}
			break;
		default:
			retval = -6;
		}
	}
	return retval;
}

/* value mod */
static inline int it_mod(ivalue_t *dst, const ivalue_t *src)
{
	int retval = 0;
	if (it_type(dst) == it_type(src) && it_type(dst) == ITYPE_INT) {
		if (it_int(src) != 0) it_int(dst) %= it_int(src);
		else retval = -1;
	}	else {
		retval = -1;
	}
	return retval;
}

/* init integer value */
static inline ivalue_t *it_init_int(ivalue_t *v, ilong value)
{
	it_init(v, ITYPE_INT);
	it_int(v) = value;
	return v;
}

/* init pointer value */
static inline ivalue_t *it_init_ptr(ivalue_t *v, const void *ptr)
{
	it_init(v, ITYPE_PTR);
	it_ptr(v) = (char*)ptr;
	return v;
}

/* init string value */
static inline ivalue_t *it_init_str(ivalue_t *v, const char *s, ilong l)
{
	it_init(v, ITYPE_STR);
	l = l < 0 ? (ilong)strlen(s) : l;
	it_sresize(v, l);
	memcpy(it_ptr(v), s, l);
	return v;
}

/* setup string reference */
static inline ivalue_t *it_strref(ivalue_t *v, const char *s, ilong l)
{
	it_init(v, ITYPE_STR);
	l = l < 0 ? (long)strlen(s) : l;
	v->size = l;
	it_ptr(v) = (char*)s;
	return v;
}

/* string copy from pointer */
static inline ivalue_t *it_strcpyc(ivalue_t *v, const char *s, ilong l)
{
	if (it_type(v) == ITYPE_STR) {
		l = l < 0 ? (ilong)strlen(s) : l;
		it_sresize(v, l);
		memcpy(it_ptr(v), s, l);
	}
	return v;
}

/* copy from a c string */
static inline ivalue_t *it_strcpyc2(ivalue_t *v, const char *s)
{
	return it_strcpyc(v, s, (ilong)strlen(s));
}

/* string cat */
static inline ivalue_t *it_strcat(ivalue_t *v, const ivalue_t *src)
{
	if (it_type(v) == ITYPE_STR) {
		iulong size = it_size(v);
		it_sresize(v, size + it_size(src));
		memcpy(it_str(v) + size, it_str(src), it_size(src));
	}
	return v;
}

/* string cat c */
static inline ivalue_t *it_strcatc(ivalue_t *v, const char *s, ilong l)
{
	ivalue_t src;
	it_strref(&src, s, l);
	it_strcat(v, &src);
	return v;
}

/* string cat c 2 */
static inline ivalue_t *it_strcatc2(ivalue_t *v, const char *s)
{
	return it_strcatc(v, s, (ilong)strlen(s));
}

/* string hash 1 inline */
static inline iulong _istrhash(const char *name, iulong len)
{
	iulong step = (len >> 5) + 1;
	iulong h = (iulong)len;
    iulong i;
    for(i = len; i >= step; i -= step)
        h = h ^ ((h << 5) + (h >> 2) + (iulong)name[i - 1]);
    return h;
}

/* hash string */
static inline void it_hashstr(ivalue_t *v)
{
	if (it_type(v) != ITYPE_STR) return;
	it_hash(v) = _istrhash(it_str(v), it_size(v));
	it_rehash(v) = 1;
}


/**********************************************************************
 * DICTIONARY OPERATION
 *
 * basic dictionary (extended hash map) interface, support:
 * - lookup and delete 
 * - integer iterator (pos)
 * - auto-type key & value
 *
 * feature: 2.1-2.2 times faster than std::map (string or integer key)
 * feature: 1.3-1.5 times faster than stdext::hash_map 
 *  
 **********************************************************************/

/* a single entry (key, value) in a dictionary */
struct IDICTENTRY
{
	ivalue_t key;				/* key		*/
	ivalue_t val;				/* val		*/
	iqueue_head queue;			/* bucket list iterator */
	ilong pos;					/* integer iterator */
	ilong sid;					/* index id */
};

/* a hash bucket in a dictionary */
struct IDICTBUCKET
{
	iqueue_head head;			/* bucket list head */
	ilong count;				/* how many entries in the bucket */
};

#ifndef IDICT_LRUSHIFT
#define IDICT_LRUSHIFT		4
#endif

#define IDICT_LRUSIZE		(1ul << IDICT_LRUSHIFT)


/*-------------------------------------------------------------------*/
/* IDICTIONARY - dictionary definition                               */
/*-------------------------------------------------------------------*/
struct IDICTIONARY
{
	struct IDICTBUCKET *table;		/* hash table */
	struct IMEMNODE nodes;			/* entries container */
	struct IVECTOR vect;			/* hash table memory */
	ilong shift;					/* hash table size shift */
	ilong mask;						/* hash table size mask */
	ilong size;						/* how many entries in the dict */
	ilong inc;						/* auto increasement */
	ilong length;					/* hash table size */
	struct IDICTENTRY *lru[IDICT_LRUSIZE];		/* lru cache */
};

typedef struct IDICTIONARY idict_t;
typedef struct IDICTENTRY idictentry_t;


/*-------------------------------------------------------------------*/
/* dictionary basic interface                                        */
/*-------------------------------------------------------------------*/

/* create dictionary */
idict_t *idict_create(void);

/* delete dictionary */
void idict_delete(idict_t *dict);


/* search pair in dictionary */
ivalue_t *idict_search(idict_t *dict, const ivalue_t *key, ilong *pos);

/* add (key, val) pair into dictionary */
ilong idict_add(idict_t *dict, const ivalue_t *key, const ivalue_t *val);

/* delete pair from dictionary */
int idict_del(idict_t *dict, const ivalue_t *key);


/* update (key, val) from dictionary */
ilong idict_update(idict_t *dict, const ivalue_t *key, const ivalue_t *val);

/* get key from position */
ivalue_t *idict_pos_get_key(idict_t *dict, ilong pos);

/* get val from position */
ivalue_t *idict_pos_get_val(idict_t *dict, ilong pos);

/* get sid from position */
ilong idict_pos_get_sid(idict_t *dict, ilong pos);

/* update from position */
void idict_pos_update(idict_t *dict, ilong pos, const ivalue_t *val);

/* delete from position */
void idict_pos_delete(idict_t *dict, ilong pos);

/* get first position */
ilong idict_pos_head(idict_t *dict);

/* get next position */
ilong idict_pos_next(idict_t *dict, ilong pos);

/* position tag */
#define idict_tag(dict, pos) (IMNODE_NODE(&((dict)->nodes), (pos)))

/* clear dictionary */
void idict_clear(idict_t *dict);


/*-------------------------------------------------------------------*/
/* directly typing interface                                         */
/*-------------------------------------------------------------------*/

/* search: key(str) val(str) */
int idict_search_ss(idict_t *dict, const char *key, ilong keysize,
	char **val, ilong *valsize);

/* search: key(int) val(str) */
int idict_search_is(idict_t *dict, ilong key, char **val, ilong *valsize);

/* search: key(str) val(int) */
int idict_search_si(idict_t *dict, const char *key, ilong keysize, 
	ilong *val);

/* search: key(int) val(int) */
int idict_search_ii(idict_t *dict, ilong key, ilong *val);

/* search: key(str) val(ptr) */
int idict_search_sp(idict_t *dict, const char *key, ilong keysize, 
	void**ptr);

/* search: key(int) val(ptr) */
int idict_search_ip(idict_t *dict, ilong key, void**ptr);

/* add: key(str) val(str) */
ilong idict_add_ss(idict_t *dict, const char *key, ilong keysize,
	const char *val, ilong valsize);

/* add: key(int) val(str) */
ilong idict_add_is(idict_t *dict, ilong key, const char *val, 
	ilong valsize);

/* add: key(str) val(int) */
ilong idict_add_si(idict_t *dict, const char *key, ilong keysize, 
	ilong val);

/* add: key(int) val(int) */
ilong idict_add_ii(idict_t *dict, ilong key, ilong val);

/* add: key(str) val(ptr) */
ilong idict_add_sp(idict_t *dict, const char *key, ilong keysize, 
	const void *ptr);

/* add: key(int) val(ptr) */
ilong idict_add_ip(idict_t *dict, ilong key, const void *ptr);

/* update: key(str) val(str) */
ilong idict_update_ss(idict_t *dict, const char *key, ilong keysize,
	const char *val, ilong valsize);

/* update: key(int) val(str) */
ilong idict_update_is(idict_t *dict, ilong key, const char *val, 
	ilong valsize);

/* update: key(str) val(int) */
ilong idict_update_si(idict_t *dict, const char *key, ilong keysize, 
	ilong val);

/* update: key(int) val(int) */
ilong idict_update_ii(idict_t *dict, ilong key, ilong val);

/* update: key(str) val(ptr) */
ilong idict_update_sp(idict_t *dict, const char *key, ilong keysize, 
	const void *ptr);

/* update: key(int) val(ptr) */
ilong idict_update_ip(idict_t *dict, ilong key, const void *ptr);

/* delete: key(str) */
int idict_del_s(idict_t *dict, const char *key, ilong keysize);

/* delete: key(int) */
int idict_del_i(idict_t *dict, ilong key);




/**********************************************************************
 * Cross-Platform Data Encode / Decode                               
 **********************************************************************/

/* encode 8 bits unsigned int */
static inline char *iencode8u(char *p, unsigned char c)
{
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const char *idecode8u(const char *p, unsigned char *c)
{
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline char *iencode16u_lsb(char *p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	*(unsigned short*)(p) = w;
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const char *idecode16u_lsb(const char *p, unsigned short *w)
{
#if IWORDS_BIG_ENDIAN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	*w = *(const unsigned short*)p;
#endif
	p += 2;
	return p;
}

/* encode 16 bits unsigned int (msb) */
static inline char *iencode16u_msb(char *p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN
	*(unsigned short*)(p) = w;
#else
	*(unsigned char*)(p + 0) = (w >> 8);
	*(unsigned char*)(p + 1) = (w & 255);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (msb) */
static inline const char *idecode16u_msb(const char *p, unsigned short *w)
{
#if IWORDS_BIG_ENDIAN
	*w = *(const unsigned short*)p;
#else
	*w = *(const unsigned char*)(p + 0);
	*w = *(const unsigned char*)(p + 1) + (*w << 8);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char *iencode32u_lsb(char *p, IUINT32 l)
{
#if IWORDS_BIG_ENDIAN
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	*(IUINT32*)p = l;
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const char *idecode32u_lsb(const char *p, IUINT32 *l)
{
#if IWORDS_BIG_ENDIAN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else 
	*l = *(const IUINT32*)p;
#endif
	p += 4;
	return p;
}

/* encode 32 bits unsigned int (msb) */
static inline char *iencode32u_msb(char *p, IUINT32 l)
{
#if IWORDS_BIG_ENDIAN
	*(IUINT32*)p = l;
#else
	*(unsigned char*)(p + 0) = (unsigned char)((l >> 24) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >>  0) & 0xff);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (msb) */
static inline const char *idecode32u_msb(const char *p, IUINT32 *l)
{
#if IWORDS_BIG_ENDIAN
	*l = *(const IUINT32*)p;
#else 
	*l = *(const unsigned char*)(p + 0);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 3) + (*l << 8);
#endif
	p += 4;
	return p;
}

/* encode 8 bits int */
static inline char *iencode8i(char *p, char c)
{
	iencode8u(p, (unsigned char)c);
	return p + 1;
}

/* decode 8 bits int */
static inline const char *idecode8i(const char *p, char *c)
{
	idecode8u(p, (unsigned char*)c);
	return p + 1;
}

/* encode 16 bits int */
static inline char *iencode16i_lsb(char *p, short w)
{
	iencode16u_lsb(p, (unsigned short)w);
	return p + 2;
}

/* decode 16 bits int */
static inline const char *idecode16i_lsb(const char *p, short *w)
{
	idecode16u_lsb(p, (unsigned short*)w);
	return p + 2;
}

/* encode 16 bits int */
static inline char *iencode16i_msb(char *p, short w)
{
	iencode16u_msb(p, (unsigned short)w);
	return p + 2;
}

/* decode 16 bits int */
static inline const char *idecode16i_msb(const char *p, short *w)
{
	idecode16u_msb(p, (unsigned short*)w);
	return p + 2;
}

/* encode 32 bits int */
static inline char *iencode32i_lsb(char *p, IINT32 l)
{
	iencode32u_lsb(p, (IUINT32)l);
	return p + 4;
}

/* decode 32 bits int */
static inline const char *idecode32i_lsb(const char *p, IINT32 *w)
{
	IUINT32 x;
	idecode32u_lsb(p, &x);
	if (x & 0x80000000) {
		*w = -(long)((~((x & 0x7ffffffful) - 1)) & 0x7ffffffful);
	}	else {
		*w = (long)x;
	}
	return p + 4;
}

/* encode 32 bits int */
static inline char *iencode32i_msb(char *p, IINT32 l)
{
	iencode32u_msb(p, (IUINT32)l);
	return p + 4;
}

/* decode 32 bits int */
static inline const char *idecode32i_msb(const char *p, IINT32 *w)
{
	IUINT32 x;
	idecode32u_msb(p, &x);
	if (x & 0x80000000) {
		*w = -(long)((~((x & 0x7ffffffful) - 1)) & 0x7ffffffful);
	}	else {
		*w = (long)x;
	}
	return p + 4;
}

/* encode float */
static inline char *iencodef_lsb(char *p, float f)
{
	union { IUINT32 intpart; float floatpart; } vv;
	vv.floatpart = f;
	return iencode32u_lsb(p, vv.intpart);
}

/* decode float */
static inline const char *idecodef_lsb(const char *p, float *f)
{
	union { IUINT32 intpart; float floatpart; } vv;
	p = idecode32u_lsb(p, &vv.intpart);
	*f = vv.floatpart;
	return p;
}

/* encode float */
static inline char *iencodef_msb(char *p, float f)
{
	IUINT32 *buf;
	void *ptr = &f;
	buf = (IUINT32*)ptr;
	return iencode32u_msb(p, *buf);
}

/* decode float */
static inline const char *idecodef_msb(const char *p, float *f)
{
	IUINT32 buf;
	void *ptr;
	p = idecode32u_msb(p, (IUINT32*)&buf);
	ptr = &buf;
	*f = *(float*)ptr;
	return p;
}

/* encode string (string = ptr + size) */
static inline char *iencodes(char *p, const void *ptr, ilong size)
{
	p = iencode16u_lsb(p, (unsigned short)size);
	if (ptr) memcpy(p, ptr, size);
	return p + size;
}

/* decode string (string = ptr + size) */
static inline const char *idecodes(const char *p, void *ptr, ilong *size)
{
	unsigned short length;
	long min = 0xffff;
	p = idecode16u_lsb(p, &length);
	if (size) {
		if (*size > 0) min = *size;
		*size = length;
	}
	if (ptr) {
		min = min < length ? min : length;
		memcpy(ptr, p, min);
	}
	return p + length;
}

/* encode c-string (endup with zero) */
static inline char *iencodestr(char *p, const char *str)
{
	long len = (long)strlen(str) + 1;
	return iencodes(p, str, len);
}

/* decode c-string (endup with zero) */
static inline const char *idecodestr(const char *p, char *str, ilong maxlen)
{
	unsigned short size;
	idecode16u_lsb(p, &size);
	if (maxlen <= 0) maxlen = size + 1;
	str[maxlen - 1] = 0;
	return idecodes(p, str, &maxlen);
}

/* encode auto-type value */
static inline char *iencodev(char *p, const ivalue_t *v)
{
	switch (it_type(v))
	{
	case ITYPE_STR:
		p = iencodes(p, it_str(v), it_size(v));
		break;
	case ITYPE_INT:
		p = iencode32i_lsb(p, it_int(v));
		break;
	case ITYPE_FLOAT:
		p = iencodef_lsb(p, it_flt(v));
		break;
	default:
		IASSERT(it_type(v) != ITYPE_PTR && it_type(v) != ITYPE_NONE);
		break;
	}
	return p;
}

/* decode auto-type value */
static inline const char *idecodev(const char *p, ivalue_t *v)
{
	unsigned short size;
	IINT32 x;
	switch (it_type(v))
	{
	case ITYPE_STR:
		size = *(unsigned short*)p;
		it_sresize(v, size);
		p = idecodes(p, &it_ptr(v), NULL);
		break;
	case ITYPE_INT:
		p = idecode32i_lsb(p, &x);
		it_int(v) = x;
		break;
	case ITYPE_FLOAT:
		p = idecodef_lsb(p, &it_flt(v));
		break;
	default:
		IASSERT(it_type(v) != ITYPE_PTR && it_type(v) != ITYPE_NONE);
		break;
	}
	return p;
}


#ifndef _MSC_VER
	#define IUINT64_CONST(__immediate) (__immediate ## ULL)
#else
	#define IUINT64_CONST(__immediate) (__immediate ## UI64)
#endif

#define IUINT64_MASK(__bits) ((((IUINT64)1) << (__bits)) - 1)

/* encode auto size unsigned integer */
static inline char *iencodeu(char *ptr, IUINT64 v)
{
	unsigned char *p = (unsigned char*)ptr;
	p[0] = (unsigned char)(v & 0x7f);
	if (v <= IUINT64_MASK(7))
		return ptr + 1;
	p[0] |= 0x80;
	p[1] = (unsigned char)((v >> 7) & 0x7f);
	if (v <= IUINT64_MASK(14))
		return ptr + 2;
	p[1] |= 0x80;
	p[2] = (unsigned char)((v >> 14) & 0x7f);
	if (v <= IUINT64_MASK(21)) 
		return ptr + 3;
	p[2] |= 0x80;
	p[3] = (unsigned char)((v >> 21) & 0x7f);
	if (v <= IUINT64_MASK(28)) 
		return ptr + 4;
	p[3] |= 0x80;
	p[4] = (unsigned char)((v >> 28) & 0x7f);
	if (v <= IUINT64_MASK(35))
		return ptr + 5;
	p[4] |= 0x80;
	p[5] = (unsigned char)((v >> 35) & 0x7f);
	if (v <= IUINT64_MASK(42))
		return ptr + 6;
	p[5] |= 0x80;
	p[6] = (unsigned char)((v >> 42) & 0x7f);
	if (v <= IUINT64_MASK(49))
		return ptr + 7;
	p[6] |= 0x80;
	p[7] = (unsigned char)((v >> 49) & 0x7f);
	if (v <= IUINT64_MASK(56))
		return ptr + 8;
	p[7] |= 0x80;
	p[8] = (unsigned char)((v >> 56) & 0x7f);
	if (v <= IUINT64_MASK(63))
		return ptr + 9;
	p[8] |= 0x80;
	p[9] = (unsigned char)((v >> 63) & 0x7f);
	return ptr + 10;
}


/* decode auto size unsigned integer */
static inline const char *idecodeu(const char *ptr, IUINT64 *v)
{
	const unsigned char *p = (const unsigned char*)ptr;
	IUINT64 x = 0;

	x |= ((IUINT32)(p[0] & 0x7f));
	if ((p[0] & 0x80) == 0) {
		v[0] = x;
		return ptr + 1;
	}

	x |= ((IUINT32)(p[1] & 0x7f)) << 7;
	if ((p[1] & 0x80) == 0) {
		v[0] = x;
		return ptr + 2;
	}

	x |= ((IUINT32)(p[2] & 0x7f)) << 14;
	if ((p[2] & 0x80) == 0) {
		v[0] = x;
		return ptr + 3;
	}

	x |= ((IUINT32)(p[3] & 0x7f)) << 21;
	if ((p[3] & 0x80) == 0) {
		v[0] = x;
		return ptr + 4;
	}

	x |= ((IUINT64)(p[4] & 0x7f)) << 28;
	if ((p[4] & 0x80) == 0) {
		v[0] = x;
		return ptr + 5;
	}

	x |= ((IUINT64)(p[5] & 0x7f)) << 35;
	if ((p[5] & 0x80) == 0) {
		v[0] = x;
		return ptr + 6;
	}

	x |= ((IUINT64)(p[6] & 0x7f)) << 42;
	if ((p[6] & 0x80) == 0) {
		v[0] = x;
		return ptr + 7;
	}

	x |= ((IUINT64)(p[7] & 0x7f)) << 49;
	if ((p[7] & 0x80) == 0) {
		v[0] = x;
		return ptr + 8;
	}

	x |= ((IUINT64)(p[8] & 0x7f)) << 56;
	if ((p[8] & 0x80) == 0) {
		v[0] = x;
		return ptr + 9;
	}

	x |= ((IUINT64)(p[9] & 0x7f)) << 63;
	v[0] = x;

	return ptr + 10;
}

/* encode auto size integer */
static inline char *iencodei(char *p, IINT64 value)
{
	IUINT64 x, y;
	y = *(IUINT64*)&value;
	if (y & ((IUINT64)1 << 63)) x = ((~y) << 1) | 1;
	else x = y << 1;
	return iencodeu(p, x);
}

/* decode auto size integer */
static inline const char *idecodei(const char *p, IINT64 *value)
{
	IUINT64 x, y;
	p = idecodeu(p, &x);
	if ((x & 1) == 0) y = x >> 1;
	else y = ~(x >> 1);
	*value = *(IINT64*)&y;
	return p;
}

/* swap byte order of int16 */
static inline unsigned short iexbyte16(unsigned short word)
{
	return ((word & 0xff) << 8) | ((word >> 8) & 0xff);
}

/* swap byte order of int32 */
static inline IUINT32 iexbyte32(IUINT32 dword)
{
	IUINT32 b1 = (dword >>  0) & 0xff;
	IUINT32 b2 = (dword >>  8) & 0xff;
	IUINT32 b3 = (dword >> 16) & 0xff;
	IUINT32 b4 = (dword >> 24) & 0xff;
	return (b1 << 24) | (b2 << 16) | (b3 << 8) | (b4);
}


/**********************************************************************
 * IRING: The struct definition of the ring buffer
 **********************************************************************/
struct IRING			/* ci-buffer type */
{
	char*data;			/* memory address */
	ilong size;			/* total mem-size */
	ilong head;			/* write pointer. */
	ilong tail;			/* read pointer.. */
};

typedef struct IRING iring_t;


/**********************************************************************
 * Macros Definition
 **********************************************************************/
#define IRING_DSIZE(c) (((c)->head >= (c)->tail)?  \
                         ((c)->head - (c)->tail):   \
                         (((c)->size - (c)->tail) + (c)->head))
#define IRING_FSIZE(c) (((c)->size - IRING_DSIZE(c)) - 1)


/**********************************************************************
 * Functions: Basic Operation on a cache
 **********************************************************************/

/* init circle cache */
void iring_init(struct IRING *cache, void *buffer, ilong size);

/* get data size */
ilong iring_dsize(const struct IRING *cache);

/* get free space size */
ilong iring_fsize(const struct IRING *cache);

/* write data into cache */
ilong iring_write(struct IRING *cache, const void *data, ilong size);

/* read data and drop them from cache */
ilong iring_read(struct IRING *cache, void *data, ilong size);

/* peek data from cache (no drop data) */
ilong iring_peek(const struct IRING *cache, void *data, ilong size);

/* drop data from cache */
ilong iring_drop(struct IRING *cache, ilong size);

/* clear cache */
void iring_clear(struct IRING *cached);

/* get flat ptr and size */
ilong iring_flat(const struct IRING *cache, void **pointer);

/* put data to given position */
ilong iring_put(struct IRING *cache, ilong pos, const void *data, ilong len);

/* get data from given position */
ilong iring_get(const struct IRING *cache, ilong pos, void *data, ilong len);

/* swap buffer ptr: returns 0 for successful, -1 for size error */
int iring_swap(struct IRING *cache, void *buffer, ilong size);

/* ring buffer ptr info: returns data size */
ilong iring_ptr(struct IRING *ring, char **p1, ilong *s1, char **p2, 
	ilong *s2);


/**********************************************************************
 * IMSTREAM: The struct definition of the memory stream descriptor
 **********************************************************************/
struct IMSTREAM
{
	struct IMEMNODE *fixed_pages;
	struct IQUEUEHEAD head;
	struct IQUEUEHEAD lru;
	iulong pos_read;
	iulong pos_write;
	iulong size;
	iulong lrusize;
	ilong hiwater;
	ilong lowater; 
};

/* init memory stream */
void ims_init(struct IMSTREAM *s, imemnode_t *fnode, ilong low, ilong high);

/* destroy memory stream */
void ims_destroy(struct IMSTREAM *s);

/* get data size */
ilong ims_dsize(const struct IMSTREAM *s);

/* write data into memory stream */
ilong ims_write(struct IMSTREAM *s, const void *ptr, ilong size);

/* read (and drop) data from memory stream */
ilong ims_read(struct IMSTREAM *s, void *ptr, ilong size);

/* peek (no drop) data from memory stream */
ilong ims_peek(const struct IMSTREAM *s, void *ptr, ilong size);

/* drop data from memory stream */
ilong ims_drop(struct IMSTREAM *s, ilong size);

/* clear stream */
void ims_clear(struct IMSTREAM *s);

/* get flat ptr and size */
ilong ims_flat(const struct IMSTREAM *s, void **pointer);



/**********************************************************************
 * 32 bits unsigned integer operation
 **********************************************************************/
static inline IUINT32 _imin(IUINT32 a, IUINT32 b) {
  return a <= b ? a : b;
}

static inline IUINT32 _imax(IUINT32 a, IUINT32 b) {
  return a >= b ? a : b;
}

static inline IUINT32 _ibound(IUINT32 lower, IUINT32 middle, IUINT32 upper) {
  return _imin(_imax(lower, middle), upper);
}

static inline long itimediff(IUINT32 later, IUINT32 earlier) 
{
	return ((IINT32)(later - earlier));
}


/**********************************************************************
 * misc operation
 **********************************************************************/
static inline int _ibit_chk(const void *data, ilong index)
{
	unsigned char *ptr = (unsigned char*)data + (index >> 3);
	int offset = (int)(index & 7);
	return (ptr[0] & (1 << offset)) >> offset;
}

static inline void _ibit_set(void *data, ilong index, int value)
{
	unsigned char *ptr = (unsigned char*)data + (index >> 3);
	unsigned char val = (unsigned char)(value & 1);
	unsigned char mask = 0;
	int offset = (int)(index & 7);
	mask = ~((unsigned char)1 << offset);
	ptr[0] = (ptr[0] & mask) | (val << offset);
}


/**********************************************************************
 * common string operation (not be defined in some compiler)
 **********************************************************************/

#define ITOUPPER(a) (((a) >= 97 && (a) <= 122) ? ((a) - 32) : (a))

#define ILONG_MAX	((ilong)((~0ul) >> 1))
#define ILONG_MIN	(-ILONG_MAX - 1)

#define IINT64_MAX	((IINT64)((~((IUINT64)0)) >> 1))
#define IINT64_MIN	(-IINT64_MAX - 1)


/* strcasestr implementation */
char* istrcasestr(char* s1, char* s2);  

/* strncasecmp implementation */
int istrncasecmp(char* s1, char* s2, size_t num);

/* strsep implementation */
char *istrsep(char **stringp, const char *delim);

/* strtol implementation */
long istrtol(const char *nptr, const char **endptr, int ibase);

/* strtoul implementation */
unsigned long istrtoul(const char *nptr, const char **endptr, int ibase);

/* istrtoll implementation */
IINT64 istrtoll(const char *nptr, const char **endptr, int ibase);

/* istrtoull implementation */
IUINT64 istrtoull(const char *nptr, const char **endptr, int ibase);

/* iltoa implementation */
int iltoa(long val, char *buf, int radix);

/* iultoa implementation */
int iultoa(unsigned long val, char *buf, int radix);

/* iltoa implementation */
int illtoa(IINT64 val, char *buf, int radix);

/* iultoa implementation */
int iulltoa(IUINT64 val, char *buf, int radix);

/* istrstrip implementation */
char *istrstrip(char *ptr, const char *delim);

/* string escape */
ilong istrsave(const char *src, ilong size, char *out);

/* string un-escape */
ilong istrload(const char *src, ilong size, char *out);

/* csv tokenizer */
const char *istrcsvtok(const char *text, ilong *next, ilong *size);


/**********************************************************************
 * ivalue_t string library
 **********************************************************************/

/* get sub string (s:start, e:endup) */
ivalue_t *it_strsub(const ivalue_t *src, ivalue_t *dst, ilong s, ilong e);

/* string compare */
int it_strcmp(const ivalue_t *src, const ivalue_t *str, ilong start);

/* string compare case insensitive */
int it_stricmp(const ivalue_t *src, const ivalue_t *str, ilong start);

/* string compare with c string */
int it_strcmpc(const ivalue_t *src, const char *str, ilong start);

/* string compare with c string (case insensitive) */
int it_stricmpc(const ivalue_t *src, const char *str, ilong start);

/* ivalue_t str strip */
ivalue_t *it_strstrip(ivalue_t *str, const ivalue_t *delim);

/* ivalue_t str strip */
ivalue_t *it_strstripc(ivalue_t *str, const char *delim);

/* it_strsep implementation */
int it_strsep(const ivalue_t *src, iulong *pos, ivalue_t *dst,
	const ivalue_t *sep);

/* it_strsepc implementation */
int it_strsepc(const ivalue_t *src, iulong *pos, ivalue_t *dst,
	const char *sep);

/* find str in src (s:start, e:endup) */
ilong it_strfind(const ivalue_t *src, const ivalue_t *str, ilong s, ilong e);

/* find str in src */
ilong it_strfind2(const ivalue_t *src, const ivalue_t *str, ilong start);

/* find str in src (s:start, e:endup) case insensitive */
ilong it_strfindi(const ivalue_t *src, const ivalue_t *str, 
	ilong s, ilong e);

/* find str in src */
ilong it_strfindi2(const ivalue_t *src, const ivalue_t *str, ilong start);

/* c: find str in src */
ilong it_strfindc(const ivalue_t *src, const char *str, ilong s, ilong e);

/* c: find str in src */
ilong it_strfindc2(const ivalue_t *src, const char *str, ilong start);

/* c: find str in src */
ilong it_strfindic(const ivalue_t *src, const char *str, ilong s, ilong e);

/* c: find str in src */
ilong it_strfindic2(const ivalue_t *src, const char *str, ilong start);

/* find from right */
ilong it_strfindr(const ivalue_t *src, const ivalue_t *str, 
	ilong s, ilong e);

/* find from right */
ilong it_strfindri(const ivalue_t *src, const ivalue_t *str, 
	ilong s, ilong e);

/* case change: change=0: upper, change=1: lowwer */
ivalue_t *it_strcase(ivalue_t *src, int change);

/* set integer */
ivalue_t *it_strsetl(ivalue_t *src, ilong val, int radix);

/* set unsigned int */
ivalue_t *it_strsetul(ivalue_t *src, iulong val, int radix);

/* append integer */
ivalue_t *it_strappendl(ivalue_t *src, ilong val, int radix);

/* append unsigned int */
ivalue_t *it_strappendul(ivalue_t *src, iulong val, int radix);

/* left just */
ivalue_t *it_strljust(ivalue_t *src, iulong width, char fill);

/* right just */
ivalue_t *it_strrjust(ivalue_t *src, iulong width, char fill);

/* middle just */
ivalue_t *it_strmiddle(ivalue_t *src, iulong width, char fill);

/* replace: if count >= 0 only the first count occurrences are replaced */
ivalue_t *it_replace(const ivalue_t *src, ivalue_t *out,
	const ivalue_t *str_old, const ivalue_t *str_new, ilong count);


/**********************************************************************
 * string list library
 **********************************************************************/
struct ISTRINGLIST
{
	ivector_t *vector;
	ivalue_t **values;
	ivalue_t none;
	ilong count;
};

typedef struct ISTRINGLIST istring_list_t;

/* create new string list */
istring_list_t* istring_list_new(void);

/* delete string list */
void istring_list_delete(istring_list_t *strings);

/* remove at position */
void istring_list_remove(istring_list_t *strings, ilong pos);

/* clear string list */
void istring_list_clear(istring_list_t *strings);

/* insert at position */
int istring_list_insert(istring_list_t *strings, ilong pos, 
	const ivalue_t *value);

/* insert at position with c string */
int istring_list_insertc(istring_list_t *strings, ilong pos, 
	const char *value, ilong size);

/* push back */
int istring_list_push_back(istring_list_t *strings, const ivalue_t *value);

/* push back c string */
int istring_list_push_backc(istring_list_t *strings, const char *value, 
	ilong size);

/* get string */
static inline ivalue_t* istring_list_get(istring_list_t *strings, ilong i) {
	if (i < 0 || i >= strings->count) return NULL;
	return strings->values[i];
}

/* get const string */
static inline const ivalue_t* istring_list_get_const(
	const istring_list_t *strings, ilong i) {
	if (i < 0 || i >= strings->count) return NULL;
	return strings->values[i];
}


/* encode into csv row */
int istring_list_csv_encode(const istring_list_t *strings, ivalue_t *csvrow);

/* decode from csv row */
istring_list_t *istring_list_csv_decode(const char *csvrow, ilong size);

/* split into strings */
istring_list_t *istring_list_split(const char *text, ilong len,
	const char *seps, ilong seplen);

/* join string list */
int istring_list_join(const istring_list_t *strings, const char *str, 
	ilong size, ivalue_t *output);


/**********************************************************************
 * BASE64 / BASE32 / BASE16
 **********************************************************************/

/* encode data as a base64 string, returns string size,
   if dst == NULL, returns how many bytes needed for encode (>=real) */
ilong ibase64_encode(const void *src, ilong size, char *dst);

/* decode a base64 string into data, returns data size 
   if dst == NULL, returns how many bytes needed for decode (>=real) */
ilong ibase64_decode(const char *src, ilong size, void *dst);

/* encode data as a base32 string, returns string size,
   if dst == NULL, returns how many bytes needed for encode (>=real) */
ilong ibase32_encode(const void *src, ilong size, char *dst);

/* decode a base32 string into data, returns data size 
   if dst == NULL, returns how many bytes needed for decode (>=real) */
ilong ibase32_decode(const char *src, ilong size, void *dst);

/* encode data as a base16 string, returns string size,
   the 'dst' output size is (2 * size). '\0' isn't appended */
ilong ibase16_encode(const void *src, ilong size, char *dst);

/* decode a base16 string into data, returns data size 
   if dst == NULL, returns how many bytes needed for decode (>=real) */
ilong ibase16_decode(const char *src, ilong size, void *dst);



/**********************************************************************
 * RC4
 **********************************************************************/

/* rc4 init */
void icrypt_rc4_init(unsigned char *box, int *x, int *y, 
	const unsigned char *key, int keylen);

/* rc4_crypt */
void icrypt_rc4_crypt(unsigned char *box, int *x, int *y, 
	const unsigned char *src, unsigned char *dst, ilong size);


/**********************************************************************
 * XOR crypt
 **********************************************************************/
static inline void icrypt_xor(const void *s, void *d, ilong c, IUINT32 m) {
	const unsigned char *ptr = (const unsigned char*)s;
	unsigned char *out = (unsigned char*)d;
	unsigned char masks[4];
	ilong i;
	masks[0] = (unsigned char)((m >> 24) & 0xff);
	masks[1] = (unsigned char)((m >> 16) & 0xff);
	masks[2] = (unsigned char)((m >>  8) & 0xff);
	masks[3] = (unsigned char)((m >>  0) & 0xff);
	for (i = 0; i < c; ptr++, out++, i++) {
		out[0] = ptr[0] ^ masks[i & 3];
	}
}

static inline void icrypt_xor_8(const void *s, void *d, ilong c, IUINT8 m) {
	const unsigned char *ptr = (const unsigned char*)s;
	unsigned char *out = (unsigned char*)d;
	for (; c > 0; ptr++, out++, c--) {
		out[0] = ptr[0] ^ m;
	}
}

static inline IUINT32 icrypt_checksum(const void *src, ilong size) {
	const unsigned char *ptr = (const unsigned char*)src;
	IUINT32 checksum = 0;
	for (; size > 0; ptr++, size--) 
		checksum += ptr[0];
	return checksum;
}


#ifdef __cplusplus
}
#endif

#endif


