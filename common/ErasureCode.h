//=====================================================================
//
// ErasureCode.h - 纠删码实现
//
// Last Modified: 2020/02/24 23:22:07
//
//=====================================================================
#ifndef _ERASURECODE_H_
#define _ERASURECODE_H_

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <vector>

#include "../incoming/rs.h"


namespace System {

//---------------------------------------------------------------------
// 纠删码实现
//---------------------------------------------------------------------
class ErasureCode
{
public:
	ErasureCode();
	virtual ~ErasureCode();

private:
	// 禁用拷贝构造和拷贝赋值
	ErasureCode(const ErasureCode&);
	ErasureCode & operator = (const ErasureCode&);

public:

	// 初始化，传入数据分片数量，冗余分片数量
	int init(int data_shards, int parity_shards);

	// 设置分片指针：0 <= index < (data_shards + parity_shards)
	void set_shard(int index, void *ptr);
	void *get_shard(int index);
	const void *get_shard(int index) const;


	// 设置 mask 数组
	inline uint8_t& operator[](int i) { return _shards_mask[i]; }
	inline const uint8_t& operator[](int i) const { return _shards_mask[i]; }

	// 编码：编码以前要 set_shard 设置好指针，成功则返回 0
	int encode(int block_size);

	// 解码：解码以前要 set_shard 设置好指针以及 mask 数组，
	// 已有数据分片填写 0，代表不需要恢复；否则填 1，代表需要恢复
	int decode(int block_size);

	// 返回当前数据分片数量
	inline int num_of_data_shards() const { return _data_shards; }

	// 返回冗余数据分片数量
	inline int num_of_parity_shards() const { return _parity_shards; }

	// 返回总分片数量
	inline int num_of_shards() const { return _shards; }

public:
	uint32_t mask;

protected:
	int _data_shards;
	int _parity_shards;
	int _shards;
	std::vector<uint8_t*> _shards_ptr;
	std::vector<uint8_t> _shards_mask;
	reed_solomon *_rs;
};


}


#endif




