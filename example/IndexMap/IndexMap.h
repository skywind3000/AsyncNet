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

	inline void free(int32_t index);

	inline int capacity() const { return _capacity; }
	inline int num_used() const { return _num_used; }
	inline int num_free() const { return _num_free; }

private:
	inline void grow();
	inline int32_t index_to_id(int32_t index) const;
	inline int32_t index_to_version(int32_t index) const;
	inline IndexNode* node_get(int32_t index);
	inline const IndexNode* node_get(int32_t index) const;

private:
	int _capacity;
	int _num_free;
	int _num_used;
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
	_num_free = 0;
	_num_used = 0;
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

inline int32_t IndexMap::index_to_id(int32_t index) const {
	return index >> INDEX_ID_SHIFT;
}

inline int32_t IndexMap::index_to_version(int32_t index) const {
	return index & INDEX_ID_MASK;
}

inline const IndexNode* IndexMap::node_get(int32_t index) const {
	int32_t id = index_to_id(index);
	if (id < 0 || id >= _capacity) return NULL;
	const IndexNode *node = _array[id];
	if (node->index != index) return NULL;
	return node;
}

inline IndexNode* IndexMap::node_get(int32_t index) {
	int32_t id = index_to_id(index);
	if (id < 0 || id >= _capacity) return NULL;
	IndexNode *node = _array[id];
	if (node->index != index) return NULL;
	return node;
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
		_num_free++;
	}
}


// alloc
int32_t IndexMap::alloc() {
	if (num_free() == 0) {
		grow();
	}
	if (num_used() * 2 >= _capacity) {
		grow();
	}
	assert(_num_free > 0);
	std::list<IndexNode*>::iterator it = _free_list.begin();
	IndexNode *node = *it;
	_free_list.erase(it);
	_used_list.push_back(node);
	node->it = _used_list.end();
	node->it--;
	assert(*(node->it) == node);
	_num_free--;
	_num_used++;
	node->state = NS_USED;
	int32_t id = index_to_id(node->index);
	int32_t version = index_to_version(node->index);
	version = (version + 1) & INDEX_ID_MASK;
	node->index = (id << INDEX_ID_SHIFT) | version;
	return node->index;
}


inline void IndexMap::free(int32_t index)
{
	IndexNode *node = node_get(index);
	if (node == NULL) {
		assert(node);
	}

}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------


#endif




