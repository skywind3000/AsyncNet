//=====================================================================
//
// IndexMap.h - 
//
// Created by skywind on 2021/12/02
// Last Modified: 2021/12/02 17:41:32
//
//=====================================================================
#ifndef _INDEX_MAP_H_
#define _INDEX_MAP_H_

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#include <vector>
#include <list>


//---------------------------------------------------------------------
// global definition
//---------------------------------------------------------------------
#define INDEX_ID_SHIFT		8
#define INDEX_ID_MASK		((1 << INDEX_ID_SHIFT) - 1)


//---------------------------------------------------------------------
// state
//---------------------------------------------------------------------
enum NodeState { 
	NS_FREE, 
	NS_USED, 
};


//---------------------------------------------------------------------
// node
//---------------------------------------------------------------------
struct IndexNode
{
	NodeState state;
	void *obj;
	int32_t index;
	std::list<IndexNode*>::iterator it;
};


//---------------------------------------------------------------------
// index map
//---------------------------------------------------------------------
class IndexMap
{
public:
	inline IndexMap();
	inline virtual ~IndexMap();

public:

	inline int32_t alloc();

	inline void free(int32_t id);

	inline int capacity() const { return _capacity; }
	inline int num_used() const { return _capacity - _available; }
	inline int num_avail() const { return _available; }

private:
	inline void grow();
	inline index2id();

private:
	int _capacity;
	int _available;
	std::vector<IndexNode*> _array;
	std::list<IndexNode*> _free_list;
	std::list<IndexNode*> _used_list;
};


//---------------------------------------------------------------------
// implements
//---------------------------------------------------------------------

// ctor
inline IndexMap::IndexMap() {
	_capacity = 0;
	_available = 0;
}

// dtor
inline IndexMap::~IndexMap() {
	for (int i = 0; i < _capacity; i++) {
		IndexNode *node = _array[i];
		if (node->state == NS_FREE) {
			_free_list.erase(node->it);
		}	else {
			_used_list.erase(node->it);
		}
		delete node;
		_array[i] = NULL;
	}
	_array.resize(0);
	assert(_free_list.size() == 0);
	assert(_used_list.size() == 0);
}

// grow
inline void IndexMap::grow() {
	int newcap = (_capacity < 8)? 8 : _capacity * 2;
	_array.resize(newcap);
	for (int i = _capacity; i < newcap; i++) {
		IndexNode *node = new IndexNode;
		_array[i] = node;
		node->state = NS_FREE;
		node->index = i << INDEX_ID_SHIFT;
		node->obj = NULL;
		_free_list.push_back(node);
		node->it = _free_list.end();
		node->it--;
		assert(*(node->it) == node);
		_available++;
	}
}

// 
int32_t IndexMap::alloc() {
	if (num_avail() == 0) {
		grow();
	}
	if (num_used() * 2 >= _capacity) {
		grow();
	}
	assert(_available > 0);
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------


#endif




