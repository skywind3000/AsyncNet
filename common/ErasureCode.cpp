//=====================================================================
//
// ErasureCode.cpp - 纠删码实现
//
// Last Modified: 2020/02/24 23:22:08
//
//=====================================================================

#include "../system/system.h"

#include "ErasureCode.h"


namespace System {

//=====================================================================
// ErasureCode
//=====================================================================

//---------------------------------------------------------------------
// ctor
//---------------------------------------------------------------------
ErasureCode::ErasureCode()
{
	static int inited;
	ithread_once(&inited, reed_solomon_init);
	_rs = NULL;
	_data_shards = 0;
	_parity_shards = 0;
	_shards = 0;
	mask = 0;
}


//---------------------------------------------------------------------
// dtor
//---------------------------------------------------------------------
ErasureCode::~ErasureCode()
{
	if (_rs) {
		reed_solomon_release(_rs);
		_rs = NULL;
	}
	_data_shards = 0;
	_parity_shards = 0;
	_shards = 0;
}


//---------------------------------------------------------------------
// 初始化，传入数据分片数量，冗余分片数量
//---------------------------------------------------------------------
int ErasureCode::init(int data_shards, int parity_shards)
{
	if (data_shards == _data_shards && parity_shards == _parity_shards) {
		if (_rs) {
			return 0;
		}
	}
	if (_rs) {
		reed_solomon_release(_rs);
		_rs = NULL;
	}
	if (data_shards > 0 && parity_shards > 0) {
		if (data_shards + parity_shards > DATA_SHARDS_MAX) {
			int shards = data_shards + parity_shards;
			fprintf(stderr, "[ErasureCode] rs shards=%d exceeds limit=%d\n", shards, DATA_SHARDS_MAX);
			fflush(stderr);
			assert(shards <= DATA_SHARDS_MAX);
			return -1;
		}
		_rs = reed_solomon_new(data_shards, parity_shards);
		if (_rs == NULL) {
			int error = reed_solomon_error();
			fprintf(stderr, "[ErasureCode] rs error=%d ds=%d ps=%d\n", error, data_shards, parity_shards);
			fflush(stderr);
		#if 1
			assert(error != 1);
			assert(error != 2);
			assert(error != 3);
			assert(error != 4);
			assert(error != 5);
			assert(_rs);
		#endif
			return -2;
		}
	}
	_data_shards = data_shards;
	_parity_shards = parity_shards;
	_shards = _data_shards + _parity_shards;
	_shards_ptr.resize(_shards);
	_shards_mask.resize(_shards);
	for (int i = 0; i < _shards; i++) {
		_shards_ptr[i] = NULL;
		_shards_mask[i] = 0;
	}
	return 0;
}


//---------------------------------------------------------------------
// 设置分片指针：0 <= index < (data_shards + parity_shards)
//---------------------------------------------------------------------
void ErasureCode::set_shard(int index, void *ptr)
{
	_shards_ptr[index] = static_cast<unsigned char*>(ptr);
}


//---------------------------------------------------------------------
// 取得分片指针：0 <= index < (data_shards + parity_shards)
//---------------------------------------------------------------------
void* ErasureCode::get_shard(int index)
{
	return _shards_ptr[index];
}


//---------------------------------------------------------------------
// 取得分片指针：0 <= index < (data_shards + parity_shards)
//---------------------------------------------------------------------
const void* ErasureCode::get_shard(int index) const
{
	return _shards_ptr[index];
}


//---------------------------------------------------------------------
// 编码：编码以前要 set_shard 设置好指针
//---------------------------------------------------------------------
int ErasureCode::encode(int block_size)
{
	if (_shards <= 0) {
		return 0;
	}
	else if (_data_shards > 0 && _parity_shards == 0) {
		return 0;
	}
	else if (_data_shards == 0 && _parity_shards > 0) {
		return 0;
	}
	if (_rs == NULL) 
		return -1000;
	for (int i = 0; i < _shards; i++) {
		if (_shards_ptr[i] == NULL)
			return -2000;
	}
	if (_parity_shards == 0) {
		return 0;
	}
	return reed_solomon_encode(_rs, &_shards_ptr[0], _shards, block_size);
}


//---------------------------------------------------------------------
// 解码：解码以前要 set_shard 设置好指针以及 mask 数组，
// 已有数据分片填写 0，代表不需要恢复；否则填 1，代表需要恢复
//---------------------------------------------------------------------
int ErasureCode::decode(int block_size)
{
	if (_shards <= 0) {
		return 0;
	}
	else if (_data_shards > 0 && _parity_shards == 0) {
		for (int i = 0; i < _data_shards; i++) {
			if (_shards_mask[i] != 0) return -1000;
		}
		return 0;
	}
	else if (_data_shards == 0 && _parity_shards > 0) {
		return 0;
	}
	if (_rs == NULL) 
		return -2000;
	return reed_solomon_reconstruct(_rs, &_shards_ptr[0], &_shards_mask[0],
			_shards, block_size);
}



}




