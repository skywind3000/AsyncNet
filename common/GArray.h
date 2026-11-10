//=====================================================================
//
// GArray.h - 
//
// Created by skywind on 2023-09-19
// Last Modified: 2023-09-19 17:13
//
//=====================================================================
#ifndef _GARRAY_H_
#define _GARRAY_H_

#include <stddef.h>
#include <stdlib.h>
#include <string.h>


//---------------------------------------------------------------------
// GAllocator
//---------------------------------------------------------------------
struct GAllocator {
	static inline void* allocate(size_t size) {
		return malloc(size);
	}
	static void deallocate(void *ptr) {
		free(ptr);
	}
};


//---------------------------------------------------------------------
// Default Allocator
//---------------------------------------------------------------------
#ifndef GALLOCATOR
#define GALLOCATOR ::GAllocator
#endif


//---------------------------------------------------------------------
// GArray
//---------------------------------------------------------------------
template <typename T, class ALLOCATOR>
class GArrayBuffer
{
private:
	T *data;
	size_t capacity;
	size_t size;

public:
	inline ~GArrayBuffer();
	inline GArrayBuffer();

	inline void resize(size_t newsize);
};


//---------------------------------------------------------------------
// ctor / dtor
//---------------------------------------------------------------------
template <typename T, class A>
inline GArrayBuffer<T, A>::GArrayBuffer(): data(NULL), capacity(0), size(0) {
}

template <typename T, class A>
inline GArrayBuffer<T, A>::~GArrayBuffer() {
	resize(0);
}


//---------------------------------------------------------------------
// resize
//---------------------------------------------------------------------
template <typename T, class A>
inline void GArrayBuffer<T, A>::resize(size_t newsize) {
}



#endif


