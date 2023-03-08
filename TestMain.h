#ifndef TESTMAIN_H
#define TESTMAIN_H

#include <Windows.h>

#define local_persist static
#define global_persist static

typedef void* handle;
typedef handle *data_handle;
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long long uint64;

#define DEFAULT_POOL_SIZE (uint64)1024
global_persist uint64 POOL_SIZE = DEFAULT_POOL_SIZE;

#define DEFAULT_POOL_ALIGNMENT (uint64)512
global_persist uint64 POOL_ALIGNMENT = DEFAULT_POOL_ALIGNMENT;

#define DEFAULT_SLOT_SIZE (uint64)64
global_persist uint64 SLOT_SIZE = DEFAULT_SLOT_SIZE;

#define DEFAULT_SLOT_ALIGNMENT (uint64)64
global_persist uint64 SLOT_ALIGNMENT = DEFAULT_SLOT_ALIGNMENT;

#define DEFAULT_INIT_VALUE (uint32)0xFFFFFFFF
global_persist uint32 INIT_VALUE = DEFAULT_INIT_VALUE;

#define POOL_ALLOWANCE_INIT (uint64)4
global_persist uint64 STARTING_POOL_ALLOWANCE = POOL_ALLOWANCE_INIT;

#define POOL_STATIC (uint8)2
#define POOL_DYNAMIC (uint8)3

#define EXIT_POOLNONDYNAMIC_ERROR 16
#define EXIT_POOLALLOC_ERROR 17
#define EXIT_INDEXALLOC_ERROR 18
#define EXIT_POOLREALLOC_ERROR 19
#define EXIT_POOLFREE_ERROR 20
#define EXIT_TOOFEWSLOTS_ERROR 21
#define EXIT_NOPOOL_ERROR 22
#define EXIT_POOLLOGALLOC_ERROR 23
#define EXIT_POOLLOGREALLOC_ERROR 24

int
AlignToSize
(
	size_t* sizeToAllign,		size_t alignment
);

typedef struct PoolDataAddressLog
{
	handle						begin;
	handle						end;
	uint32						slots;
}pool_log;
global_persist pool_log POOL_LOG;

struct block_handle_index
{
	handle						begin;
	handle						end;
};

typedef struct StaticPoolData
{
	handle						begin;
	handle						end;
	struct block_handle_index	index;
	uint32						openSlots;
	uint32						voids;
	uint32						freeIndex;
}pool_static, *pool_handle;

typedef struct BlockBody
{	
	data_handle					data;	
	uint32						elementCount;
	uint32						elementSize;
	pool_handle					inPool;
	uint8(*set)(struct BlockBody* target, void* data, uint32 atElement);
	uint8(*delete)(struct BlockBody* target, uint32 elementToDelete);
	uint8(*resize)(struct BlockBody* target, uint32 newNumElements);
	uint8(*release)(struct BlockBody* target);
}block, *block_handle;

void
DBSetInitValues
(
	uint64			newSlotSize,
	uint64			newSlotAlignment,
	uint64			newPoolSize,
	uint64			newPoolAlignment
);

uint8
DBCreatePool
(	
	pool_handle		poolToFill
);

uint8
DBFreePool
(
	pool_handle		poolToFree
);

uint8
DBAllClear();

uint32
DBBlockSize
(
	block_handle	blockToCheck
);

uint32
DBSlotCount
(
	block_handle	blockToCheck
);

block_handle
DBBuildBlock
(
	uint32			numElements,
	uint64			dataSize,
	pool_handle		targetPool
);
#endif
