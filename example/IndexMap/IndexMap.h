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
#include <vector>
#include <list>


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
	int32_t id;
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

private:

private:
	std::vector<IndexNode> _array;
	std::list<IndexNode*> _open_list;
	std::list<IndexNode*> _close_list;
};


//---------------------------------------------------------------------
// 
//---------------------------------------------------------------------

// ctor
inline IndexMap::IndexMap() {
}

// dtor
inline IndexMap::~IndexMap() {
}


#endif




