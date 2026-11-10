//=====================================================================
//
// IndexAllocator.h - Allocate index
//
// Last Modified: 2025/11/10 17:30:55
//
//=====================================================================
#ifndef _INDEX_ALLOCATOR_H_
#define _INDEX_ALLOCATOR_H_

#include <stddef.h>
#include <stdexcept>

#include "../system/system.h"



//---------------------------------------------------------------------
// Namespace Begin
//---------------------------------------------------------------------
NAMESPACE_BEGIN(System);


//---------------------------------------------------------------------
// IndexAllocator
//---------------------------------------------------------------------
class IndexAllocator
{
public:
	inline virtual ~IndexAllocator();
	inline IndexAllocator();
	inline IndexAllocator(IndexAllocator &&src);

	IndexAllocator(const IndexAllocator &) = delete;
	IndexAllocator& operator = (const IndexAllocator &) = delete;
	IndexAllocator& operator = (IndexAllocator &&) = delete;

public:

	// allocate an node
	inline int NewIndex();

	// delete an node
	inline void DeleteIndex(int index);

	// get pointer by index
	inline void*& operator[](int index);

	// get const pointer by index
	inline void* const& operator[](int index) const;

	// get size
	inline size_t size() const { return nodes->node_used; }

	// get first index
	inline int FirstIndex() const;

	// iterate to next index
	inline int NextIndex(int index) const;

	// iterate to previous index
	inline int PrevIndex(int index) const;


private:
	IMEMNODE *nodes;
	void *dummy;
	std::vector<void*> ptrs;
};


//---------------------------------------------------------------------
// implementation
//---------------------------------------------------------------------

// dtor
inline IndexAllocator::~IndexAllocator() {
	if (nodes) {
		imnode_delete(nodes);
		nodes = NULL;
	}
}

// ctor
inline IndexAllocator::IndexAllocator() {
	nodes = imnode_create(4, 1024);
	dummy = NULL;
}

// move ctor
inline IndexAllocator::IndexAllocator(IndexAllocator &&src):
	ptrs(std::move(src.ptrs))
{
	nodes = src.nodes;
	src.nodes = NULL;
	dummy = NULL;
}


// allocate an node
inline int IndexAllocator::NewIndex() {
	if (nodes == NULL) {
		throw std::runtime_error("IndexAllocator has been moved");
	}
	int index = (int)imnode_new(nodes);
	while (ptrs.size() <= (size_t)index) {
		ptrs.push_back(NULL);
	}
	ptrs[index] = NULL;
	return index;
}


// delete an node
inline void IndexAllocator::DeleteIndex(int index) {
	if (index < 0 || index >= (int)ptrs.size()) {
		throw std::out_of_range("IndexAllocator delete index error");
	}
	if (nodes == NULL) {
		throw std::runtime_error("IndexAllocator has been moved");
	}
	imnode_del(nodes, index);
}


// get pointer by index
inline void*& IndexAllocator::operator[](int index) {
	if (index < 0 || index >= (int)ptrs.size()) {
		throw std::out_of_range("IndexAllocator index error");
	}
	return ptrs[index];
}

// get const pointer by index
inline void* const& IndexAllocator::operator[](int index) const {
	if (index < 0 || index >= (int)ptrs.size()) {
		throw std::out_of_range("IndexAllocator index error");
	}
	return ptrs[index];
}

// get first index
inline int IndexAllocator::FirstIndex() const {
	return (int)imnode_head(nodes);
}

// iterate to next index
inline int IndexAllocator::NextIndex(int index) const {
	if (nodes == NULL) return -1;
	if (index < 0) return -1;
	if (index >= nodes->node_max) return -1;
	return (int)IMNODE_NEXT(nodes, index);
}

// iterate to previous index
inline int IndexAllocator::PrevIndex(int index) const {
	if (nodes == NULL) return -1;
	if (index < 0) return -1;
	if (index >= nodes->node_max) return -1;
	return (int)IMNODE_PREV(nodes, index);
}



//---------------------------------------------------------------------
// Namespace End
//---------------------------------------------------------------------
NAMESPACE_END(System);


#endif


