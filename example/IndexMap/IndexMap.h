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

#include <iterator>
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

	int32_t index_first() const;
	int32_t index_last() const;
	int32_t index_next(int32_t index) const;
	int32_t index_prev(int32_t index) const;

private:
	inline int32_t index_to_version(int32_t index) const;
	inline int32_t index_to_id(int32_t index) const;
	inline const IndexNode *index_to_node(int32_t index) const;
	inline IndexNode *index_to_node(int32_t index);
	inline void grow();

private:
	int _capacity;
	int _num_used;
	int _num_free;
	IndexNode **_nodes;
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
	_num_used = 0;
	_num_free = 0;
	_nodes = NULL;
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
	_nodes = NULL;
	assert(_free_list.size() == 0);
	assert(_used_list.size() == 0);
	_capacity = 0;
	_num_used = 0;
	_num_free = 0;
}

inline int32_t IndexMap::index_to_version(int32_t index) const {
	return index & INDEX_ID_MASK;
}

inline int32_t IndexMap::index_to_id(int32_t index) const {
	return index >> INDEX_ID_SHIFT;
}

inline const IndexNode *IndexMap::index_to_node(int32_t index) const {
	int32_t id = index_to_id(index);
	if (id < 0 || id >= _capacity) return NULL;
	const IndexNode *node = _nodes[id];
	if (node->index != index) return NULL;
	return node;
}

inline IndexNode *IndexMap::index_to_node(int32_t index) {
	const IndexNode *node = 
		static_cast<const IndexMap&>(*this).index_to_node(index);
	return const_cast<IndexNode*>(node);
}

// grow
inline void IndexMap::grow() {
	int newcap = (_capacity < 8)? 8 : _capacity * 2;
	_array.resize(newcap);
	_nodes = &_array[0];
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
	_capacity = newcap;
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
	node->state = NS_USED;
	_num_used++;
	_num_free--;
	int32_t version = index_to_version(node->index);
	int32_t id = index_to_id(node->index);
	version = (version + 1) & INDEX_ID_MASK;
	node->index = (id << INDEX_ID_SHIFT) | version;
	return node->index;
}


inline void IndexMap::free(int32_t index)
{
	IndexNode *node = index_to_node(index);
	if (node == NULL) {
		assert(node);
		return;
	}
	if (node->state != NS_USED) {
		assert(node->state == NS_USED);
		return;
	}
	_used_list.erase(node->it);
	_free_list.push_back(node);
	node->it = _free_list.end();
	node->it--;
	assert(*(node->it) == node);
	_num_used--;
	_num_free++;
	node->state = NS_FREE;
}

int32_t IndexMap::index_first() const {
	std::list<IndexNode*>::const_iterator it = _used_list.begin();
	if (it == _used_list.end()) return -1;
	return (*it)->index;
}

int32_t IndexMap::index_last() const {
	std::list<IndexNode*>::const_iterator it = _used_list.end();
	if (it == _used_list.begin()) return -1;
	it--;
	return (*it)->index;
}

int32_t IndexMap::index_next(int32_t index) const {
	const IndexNode *node = index_to_node(index);
	if (node == NULL) return -1;
	if (node->state != NS_USED) return -1;
	std::list<IndexNode*>::const_iterator it = node->it;
	it++;
	if (it == _used_list.end()) return -1;
	return (*it)->index;
}

int32_t IndexMap::index_prev(int32_t index) const {
	const IndexNode *node = index_to_node(index);
	if (node == NULL) return -1;
	if (node->state != NS_USED) return -1;
	std::list<IndexNode*>::const_iterator it = node->it;
	if (it == _used_list.begin()) return -1;
	it--;
	return (*it)->index;
}


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------


#endif




