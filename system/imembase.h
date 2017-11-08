/**********************************************************************
 *
 * imembase.h - basic interface of memory operation
 * skywind3000 (at) gmail.com, 2006-2016
 *
 **********************************************************************/

#ifndef __IMEMBASE_H__
#define __IMEMBASE_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>


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
#include <limits.h>
#if UINT_MAX == 0xFFFFU
	typedef unsigned long ISTDUINT32; 
	typedef long ISTDINT32;
#else
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#endif
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


/*--------------------------------------------------------------------*/
/* INLINE                                                             */
/*--------------------------------------------------------------------*/
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

#if (!defined(__cplusplus)) && (!defined(inline))
#define inline INLINE
#endif

/* you can change this by config.h or predefined macro */
#ifndef ASSERTION
#define ASSERTION(x) ((void)0)
#endif


/*====================================================================*/
/* IULONG/ILONG (ensure sizeof(iulong) == sizeof(void*))              */
/*====================================================================*/
#ifndef __IULONG_DEFINED
#define __IULONG_DEFINED
typedef ptrdiff_t ilong;
typedef size_t iulong;
#endif

#ifdef __cplusplus
extern "C" {
#endif


/*====================================================================*/
/* IALLOCATOR                                                         */
/*====================================================================*/
struct IALLOCATOR
{
    void *(*alloc)(struct IALLOCATOR *, size_t);
    void (*free)(struct IALLOCATOR *, void *);
	void *(*realloc)(struct IALLOCATOR *, void *, size_t);
    void *udata;
};

void* internal_malloc(struct IALLOCATOR *allocator, size_t size);
void internal_free(struct IALLOCATOR *allocator, void *ptr);
void* internal_realloc(struct IALLOCATOR *allocator, void *ptr, size_t size);


/*====================================================================*/
/* IKMEM INTERFACE                                                    */
/*====================================================================*/
extern struct IALLOCATOR *ikmem_allocator;

void* ikmem_malloc(size_t size);
void* ikmem_realloc(void *ptr, size_t size);
void ikmem_free(void *ptr);


/*====================================================================*/
/* IVECTOR                                                            */
/*====================================================================*/
struct IVECTOR
{
	unsigned char *data;       
	size_t size;      
	size_t capacity;       
	struct IALLOCATOR *allocator;
};

void iv_init(struct IVECTOR *v, struct IALLOCATOR *allocator);
void iv_destroy(struct IVECTOR *v);
int iv_resize(struct IVECTOR *v, size_t newsize);
int iv_reserve(struct IVECTOR *v, size_t newsize);

size_t iv_pop(struct IVECTOR *v, void *data, size_t size);
int iv_push(struct IVECTOR *v, const void *data, size_t size);
int iv_insert(struct IVECTOR *v, size_t pos, const void *data, size_t size);
int iv_erase(struct IVECTOR *v, size_t pos, size_t size);

#define iv_size(v) ((v)->size)
#define iv_data(v) ((v)->data)

#define iv_entry(v, type) ((type*)iv_data(v))

#define iv_obj_index(v, type, index) (iv_entry(v, type)[index])
#define iv_obj_push(v, type, objptr) iv_push(v, objptr, sizeof(type))
#define iv_obj_pop(v, type, objptr) iv_pop(v, objptr, sizeof(type))
#define iv_obj_size(v, type) (((v)->size) / sizeof(type))
#define iv_obj_capacity(v, type) (((v)->capacity) / sizeof(type))
#define iv_obj_resize(v, type, count) iv_resize(v, (count) * sizeof(type))
#define iv_obj_reserve(v, type, count) iv_reserve(v, (count) * sizeof(type))

#define iv_obj_insert(v, type, pos, objptr) \
	iv_insert(v, (pos) * sizeof(type), objptr, sizeof(type))

#define iv_obj_erase(v, type, pos, count) \
	iv_erase(v, (pos) * sizeof(type), (count) * sizeof(type))


#define IROUND_SIZE(b)    (((size_t)1) << (b))
#define IROUND_UP(s, n)   (((s) + (n) - 1) & ~(((size_t)(n)) - 1))


/*====================================================================*/
/* IMEMNODE                                                           */
/*====================================================================*/
struct IMEMNODE
{
	struct IALLOCATOR *allocator;   /* memory allocator        */

	struct IVECTOR vprev;           /* prev node link vector   */
	struct IVECTOR vnext;           /* next node link vector   */
	struct IVECTOR vnode;           /* node information data   */
	struct IVECTOR vdata;           /* node data buffer vector */
	struct IVECTOR vmode;           /* mode of allocation      */
	ilong *mprev;                   /* prev node array         */
	ilong *mnext;                   /* next node array         */
	ilong *mnode;                   /* node info array         */
	void **mdata;                   /* node data array         */
	ilong *mmode;                   /* node mode array         */
	ilong *extra;                   /* extra user data         */
	ilong node_free;                /* number of free nodes    */
	ilong node_used;                /* number of allocated     */
	ilong node_max;                 /* number of all nodes     */
	ilong grow_limit;               /* limit of growing        */

	ilong node_size;                /* node data fixed size    */
	ilong node_shift;               /* node data size shift    */

	struct IVECTOR vmem;            /* mem-pages in the pool   */
	char **mmem;                    /* mem-pages array         */
	ilong mem_max;                  /* max num of memory pages */
	ilong mem_count;                /* number of mem-pages     */

	ilong list_open;                /* the entry of open-list  */
	ilong list_close;               /* the entry of close-list */
	ilong total_mem;                /* total memory size       */
};


void imnode_init(struct IMEMNODE *mn, ilong nodesize, struct IALLOCATOR *ac);
void imnode_destroy(struct IMEMNODE *mnode);
ilong imnode_new(struct IMEMNODE *mnode);
void imnode_del(struct IMEMNODE *mnode, ilong index);
ilong imnode_head(const struct IMEMNODE *mnode);
ilong imnode_next(const struct IMEMNODE *mnode, ilong index);
ilong imnode_prev(const struct IMEMNODE *mnode, ilong index);
void*imnode_data(struct IMEMNODE *mnode, ilong index);
const void* imnode_data_const(const struct IMEMNODE *mnode, ilong index);

#define IMNODE_NODE(mnodeptr, i) ((mnodeptr)->mnode[i])
#define IMNODE_PREV(mnodeptr, i) ((mnodeptr)->mprev[i])
#define IMNODE_NEXT(mnodeptr, i) ((mnodeptr)->mnext[i])
#define IMNODE_DATA(mnodeptr, i) ((mnodeptr)->mdata[i])
#define IMNODE_MODE(mnodeptr, i) ((mnodeptr)->mmode[i])


/*====================================================================*/
/* LIST DEFINITION                                                   */
/*====================================================================*/
#ifndef __ILIST_DEF__
#define __ILIST_DEF__

struct ILISTHEAD {
	struct ILISTHEAD *next, *prev;
};

typedef struct ILISTHEAD ilist_head;


/*--------------------------------------------------------------------*/
/* list init                                                         */
/*--------------------------------------------------------------------*/
#define ILIST_HEAD_INIT(name) { &(name), &(name) }
#define ILIST_HEAD(name) \
	struct ILISTHEAD name = ILIST_HEAD_INIT(name)

#define ILIST_INIT(ptr) ( \
	(ptr)->next = (ptr), (ptr)->prev = (ptr))

#define IOFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define ICONTAINEROF(ptr, type, member) ( \
		(type*)( ((char*)((type*)ptr)) - IOFFSETOF(type, member)) )

#define ILIST_ENTRY(ptr, type, member) ICONTAINEROF(ptr, type, member)


/*--------------------------------------------------------------------*/
/* list operation                                                    */
/*--------------------------------------------------------------------*/
#define ILIST_ADD(node, head) ( \
	(node)->prev = (head), (node)->next = (head)->next, \
	(head)->next->prev = (node), (head)->next = (node))

#define ILIST_ADD_TAIL(node, head) ( \
	(node)->prev = (head)->prev, (node)->next = (head), \
	(head)->prev->next = (node), (head)->prev = (node))

#define ILIST_DEL_BETWEEN(p, n) ((n)->prev = (p), (p)->next = (n))

#define ILIST_DEL(entry) (\
	(entry)->next->prev = (entry)->prev, \
	(entry)->prev->next = (entry)->next, \
	(entry)->next = 0, (entry)->prev = 0)

#define ILIST_DEL_INIT(entry) do { \
	ILIST_DEL(entry); ILIST_INIT(entry); } while (0)

#define ILIST_IS_EMPTY(entry) ((entry) == (entry)->next)

#define ilist_init		ILIST_INIT
#define ilist_entry		ILIST_ENTRY
#define ilist_add		ILIST_ADD
#define ilist_add_tail	ILIST_ADD_TAIL
#define ilist_del		ILIST_DEL
#define ilist_del_init	ILIST_DEL_INIT
#define ilist_is_empty	ILIST_IS_EMPTY

#define ILIST_FOREACH(iterator, head, TYPE, MEMBER) \
	for ((iterator) = ilist_entry((head)->next, TYPE, MEMBER); \
		&((iterator)->MEMBER) != (head); \
		(iterator) = ilist_entry((iterator)->MEMBER.next, TYPE, MEMBER))

#define ilist_foreach(iterator, head, TYPE, MEMBER) \
	ILIST_FOREACH(iterator, head, TYPE, MEMBER)

#define ilist_foreach_entry(pos, head) \
	for( (pos) = (head)->next; (pos) != (head) ; (pos) = (pos)->next )
	

#define __ilist_splice(list, head) do {	\
		ilist_head *first = (list)->next, *last = (list)->prev; \
		ilist_head *at = (head)->next; \
		(first)->prev = (head), (head)->next = (first);		\
		(last)->next = (at), (at)->prev = (last); }	while (0)

#define ilist_splice(list, head) do { \
	if (!ilist_is_empty(list)) __ilist_splice(list, head); } while (0)

#define ilist_splice_init(list, head) do {	\
	ilist_splice(list, head);	ilist_init(list); } while (0)


#ifdef _MSC_VER
#pragma warning(disable:4311)
#pragma warning(disable:4312)
#pragma warning(disable:4996)
#endif

#endif


/*====================================================================*/
/* IMUTEX - mutex interfaces                                          */
/*====================================================================*/
#ifndef IMUTEX_TYPE

#ifndef IMUTEX_DISABLE
#if (defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64))
#if ((!defined(_M_PPC)) && (!defined(_M_PPC_BE)) && (!defined(_XBOX)))
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#ifndef WIN32_LEAN_AND_MEAN  
#define WIN32_LEAN_AND_MEAN  
#endif
#include <windows.h>
#else
#ifndef _XBOX
#define _XBOX
#endif
#include <xtl.h>
#endif

#define IMUTEX_TYPE         CRITICAL_SECTION
#define IMUTEX_INIT(m)      InitializeCriticalSection((CRITICAL_SECTION*)(m))
#define IMUTEX_DESTROY(m)   DeleteCriticalSection((CRITICAL_SECTION*)(m))
#define IMUTEX_LOCK(m)      EnterCriticalSection((CRITICAL_SECTION*)(m))
#define IMUTEX_UNLOCK(m)    LeaveCriticalSection((CRITICAL_SECTION*)(m))

#elif defined(__unix) || defined(__unix__) || defined(__MACH__)
#include <unistd.h>
#include <pthread.h>
#define IMUTEX_TYPE         pthread_mutex_t
#define IMUTEX_INIT(m)      pthread_mutex_init((pthread_mutex_t*)(m), 0)
#define IMUTEX_DESTROY(m)   pthread_mutex_destroy((pthread_mutex_t*)(m))
#define IMUTEX_LOCK(m)      pthread_mutex_lock((pthread_mutex_t*)(m))
#define IMUTEX_UNLOCK(m)    pthread_mutex_unlock((pthread_mutex_t*)(m))
#endif
#endif

#ifndef IMUTEX_TYPE
#define IMUTEX_TYPE         int
#define IMUTEX_INIT(m)      { (*(m)) = (*(m)); }
#define IMUTEX_DESTROY(m)   { (*(m)) = (*(m)); }
#define IMUTEX_LOCK(m)      { (*(m)) = (*(m)); }
#define IMUTEX_UNLOCK(m)    { (*(m)) = (*(m)); }
#endif

#endif



/*====================================================================*/
/* IVECTOR / IMEMNODE MANAGEMENT                                      */
/*====================================================================*/

typedef struct IVECTOR ib_vector;
typedef struct IMEMNODE ib_memnode;

ib_vector *iv_create(void);
void iv_delete(ib_vector *vec);

ib_memnode *imnode_create(ilong nodesize, int grow_limit);
void imnode_delete(ib_memnode *);


/*--------------------------------------------------------------------*/
/* Collection - Array                                                 */
/*--------------------------------------------------------------------*/

struct ib_array;
typedef struct ib_array ib_array;

ib_array *ib_array_new(void (*destroy_func)(void*));
void ib_array_delete(ib_array *array);
void ib_array_reserve(ib_array *array, size_t new_size);
size_t ib_array_size(const ib_array *array);
void** ib_array_ptr(ib_array *array);
void* ib_array_index(ib_array *array, size_t index);
const void* ib_array_const_index(const ib_array *array, size_t index);
void ib_array_push(ib_array *array, void *item);
void ib_array_push_left(ib_array *array, void *item);
void ib_array_replace(ib_array *array, size_t index, void *item);
void* ib_array_pop(ib_array *array);
void* ib_array_pop_left(ib_array *array);
void ib_array_remove(ib_array *array, size_t index);
void ib_array_insert_before(ib_array *array, size_t index, void *item);
void* ib_array_pop_at(ib_array *array, size_t index);
void ib_array_for_each(ib_array *array, void (*iterator)(void *item));

void ib_array_sort(ib_array *array, 
		int (*compare)(const void*, const void*));

ilong ib_array_search(const ib_array *array, 
		int (*compare)(const void*, const void*),
		const void *item, 
		ilong start_pos);

ilong ib_array_bsearch(const ib_array *array,
		int (*compare)(const void*, const void*),
		const void *item);


/*====================================================================*/
/* ib_node - binary search tree (can be used in rbtree & avl)         */
/* use array for left/right child pointers to reduce cpu branches     */
/* color won't be packed into pointers (can work without alignment)   */
/*====================================================================*/
struct ib_node
{
	struct ib_node *child[2];   /* 0 for left, 1 for right, reduce branch */
	struct ib_node *parent;     /* pointing to node itself for empty node */
	unsigned int color;         /* can also be used as balance / height */
};

struct ib_root
{
	struct ib_node *node;		/* root node */
};


/*--------------------------------------------------------------------*/
/* macros                                                             */
/*--------------------------------------------------------------------*/
#define IB_LEFT    0        /* left child index */
#define IB_RIGHT   1        /* right child index */

#define IB_RED     0
#define IB_BLACK   1

#define IB_OFFSET(TYPE, MEMBER)    ((size_t) &((TYPE *)0)->MEMBER)

#define IB_NODE2DATA(n, o)    ((void *)((size_t)(n) - (o)))
#define IB_DATA2NODE(d, o)    ((struct ib_node*)((size_t)(d) + (o)))

#define IB_ENTRY(ptr, type, member) \
	IB_NODE2DATA(ptr, IB_OFFSET(type, member))

#define ib_node_init(node) do { ((node)->parent) = (node); } while (0)
#define ib_node_empty(node) ((node)->parent == (node))


/*--------------------------------------------------------------------*/
/* rbtree - node manipulation                                         */
/*--------------------------------------------------------------------*/
struct ib_node *ib_node_first(struct ib_root *root);
struct ib_node *ib_node_last(struct ib_root *root);
struct ib_node *ib_node_next(struct ib_node *node);
struct ib_node *ib_node_prev(struct ib_node *node);

void ib_node_insert_color(struct ib_node *node, struct ib_root *root);
void ib_node_erase(struct ib_node *node, struct ib_root *root);

void ib_node_replace(struct ib_node *victim, struct ib_node *newnode,
		struct ib_root *root);

static inline void ib_node_link(struct ib_node *node, struct ib_node *parent,
		struct ib_node **ib_link) {
	node->parent = parent;
	node->color = IB_RED;
	node->child[0] = node->child[1] = NULL;
	ib_link[0] = node;
}


/*--------------------------------------------------------------------*/
/* rbtree - node templates                                            */
/*--------------------------------------------------------------------*/
#define ib_node_find(root, what, compare_fn, result_node, result_index) do {\
		struct ib_node *__n = (root)->node; \
		(result_node) = NULL; \
		(result_index) = 0; \
		while (__n) { \
			int __hr = (compare_fn)(what, __n); \
			(result_node) = __n; \
			if (__hr == 0) { (result_index) = -1; break; } \
			else if (__hr < 0) { __n = __n->child[0]; (result_index) = 0; } \
			else { __n = __n->child[1]; (result_index) = 1; } \
		} \
	}   while (0)


#define ib_node_add(root, newnode, compare_fn, duplicate_node) do { \
		struct ib_node **__link = &((root)->node); \
		struct ib_node *__parent = NULL; \
		struct ib_node *__duplicate = NULL; \
		int __hr = 1; \
		while (__link[0]) { \
			__parent = __link[0]; \
			__hr = (compare_fn)(newnode, __parent); \
			if (__hr == 0) { __duplicate = __parent; break; } \
			else { __link = &(__parent->child[(__hr < 0)? 0 : 1]); } \
		} \
		(duplicate_node) = __duplicate; \
		if (__duplicate == NULL) { \
			ib_node_link(newnode, __parent, __link); \
			ib_node_insert_color(newnode, root); \
		} \
	}   while (0)



/*--------------------------------------------------------------------*/
/* rbtree - friendly interface                                        */
/*--------------------------------------------------------------------*/
struct ib_tree
{
	struct ib_root root;		/* rbtree root */
	size_t offset;				/* node offset in user data structure */
	size_t size;                /* size of user data structure */
	size_t count;				/* node count */
	/* returns 0 for equal, -1 for n1 < n2, 1 for n1 > n2 */
	int (*compare)(const void *n1, const void *n2);
};


/* initialize rbtree, use IB_OFFSET(type, member) for "offset"
 * eg:
 *     ib_tree_init(&mytree, mystruct_compare,
 *          sizeof(struct mystruct_t), 
 *          IB_OFFSET(struct mystruct_t, node));
 */
void ib_tree_init(struct ib_tree *tree,
		int (*compare)(const void*, const void*), size_t size, size_t offset);

void *ib_tree_first(struct ib_tree *tree);
void *ib_tree_last(struct ib_tree *tree);
void *ib_tree_next(struct ib_tree *tree, void *data);
void *ib_tree_prev(struct ib_tree *tree, void *data);

/* require a temporary user structure (data) which contains the key */
void *ib_tree_find(struct ib_tree *tree, const void *data);
void *ib_tree_nearest(struct ib_tree *tree, const void *data);

/* returns NULL for success, otherwise returns conflict node with same key */
void *ib_tree_add(struct ib_tree *tree, void *data);

void ib_tree_remove(struct ib_tree *tree, void *data);
void ib_tree_replace(struct ib_tree *tree, void *victim, void *newdata);

void ib_tree_clear(struct ib_tree *tree, void (*destroy)(void *data));




#ifdef __cplusplus
}
#endif

#endif


