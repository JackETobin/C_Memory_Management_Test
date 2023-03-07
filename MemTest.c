#include "TestMain.h"

uint8 
AlignToSize(size_t *sizeToAlign, size_t alignment)
{
	size_t Misalignment = *sizeToAlign % alignment;
	if (!Misalignment)
		return EXIT_SUCCESS;
	*sizeToAlign += (alignment - Misalignment);
	if ((*sizeToAlign % alignment))
		return EXIT_ALIGNMENTFAILURE_ERROR;

	return EXIT_SUCCESS;
}

void
DBSetInitValues
(
	uint64			newSlotSize,
	uint64			newSlotAlignment,
	uint64			newPoolSize,
	uint64			newPoolAlignment
)
{
	if (newPoolAlignment)
	{
		POOL_ALIGNMENT = newPoolAlignment;
		if(!newPoolSize)
			AlignToSize(&POOL_SIZE, POOL_ALIGNMENT);
	}
	if (newPoolSize)
	{		
		AlignToSize(&newPoolSize, POOL_ALIGNMENT);
		POOL_SIZE = newPoolSize;
	}
	if (newSlotAlignment)
	{
		SLOT_ALIGNMENT = newSlotAlignment;
		if (!newSlotSize)
			AlignToSize(&SLOT_SIZE, SLOT_ALIGNMENT);
	}
	if (newSlotSize)
	{		
		if (newSlotSize < SLOT_ALIGNMENT)
			SLOT_ALIGNMENT = newSlotSize;

		AlignToSize(&newSlotSize, SLOT_ALIGNMENT);
		SLOT_SIZE = newSlotSize;
	}
}

uint8 
DBPoolLogAlloc()
{
	POOL_LOG.begin = LocalAlloc(LMEM_FIXED, STARTING_POOL_ALLOWANCE * sizeof(pool_handle*));
	if (!POOL_LOG.begin)
		return EXIT_POOLLOGALLOC_ERROR;

	POOL_LOG.end = POOL_LOG.begin + (STARTING_POOL_ALLOWANCE * sizeof(pool_handle*));
	uint32 PoolLogInitCounter = (POOL_LOG.end - POOL_LOG.begin) / sizeof(pool_handle*);
	POOL_LOG.slots = PoolLogInitCounter;
	while (PoolLogInitCounter--)
		*((pool_handle*)POOL_LOG.begin + PoolLogInitCounter) = NULL;

	return EXIT_SUCCESS;
}

uint8 
DBPoolLogExpand()
{
	uint32 LogSize = (POOL_LOG.end - POOL_LOG.begin) / sizeof(pool_handle*);
	uint32 NewSize = LogSize * 2;
	POOL_LOG.begin = LocalReAlloc(POOL_LOG.begin, (size_t)(NewSize * sizeof(pool_handle*)), LMEM_MOVEABLE);
	if (!POOL_LOG.begin)
		return EXIT_POOLLOGREALLOC_ERROR;

	POOL_LOG.end = POOL_LOG.begin + (NewSize * sizeof(pool_handle*));
	POOL_LOG.slots = (NewSize - LogSize);
	LogSize -= 1;
	while (LogSize++ < (NewSize - 1))
		*((pool_handle*)POOL_LOG.begin + LogSize) = NULL;

	return EXIT_SUCCESS;
}

uint8 
DBCreatePool(pool_handle poolToFill)
{
	if (!POOL_LOG.begin)
		DBPoolLogAlloc();

	uint64 PoolSize = POOL_SIZE;
	AlignToSize(&PoolSize, POOL_ALIGNMENT);
	uint32 NumSlots = PoolSize / SLOT_SIZE;
	uint64 InitializationCounter = PoolSize / sizeof(uint32*);
	uint64 BlockHandleIndexSize = (NumSlots) * sizeof(void*);

	poolToFill->begin = LocalAlloc(LMEM_FIXED, PoolSize);
	if (!poolToFill->begin)
		return EXIT_POOLALLOC_ERROR;

	poolToFill->index.begin = LocalAlloc(LMEM_FIXED, BlockHandleIndexSize);
	if (!poolToFill->index.begin)
		return EXIT_INDEXALLOC_ERROR;

	poolToFill->end = poolToFill->begin + PoolSize;
	poolToFill->index.end = poolToFill->index.begin + BlockHandleIndexSize;
	poolToFill->openSlots = NumSlots;

	while (InitializationCounter--)
		*((uint32*)poolToFill->begin + InitializationCounter) = INIT_VALUE;
	
	if (!POOL_LOG.slots)	
		DBPoolLogExpand();
	
	uint32 LogSize = (POOL_LOG.end - POOL_LOG.begin) / sizeof(pool_handle*);
	for (uint32 i = 0; i < LogSize; i++)
	{
		if (!*((pool_handle*)POOL_LOG.begin + i))
		{
			*((pool_handle*)POOL_LOG.begin + i) = poolToFill;
			poolToFill->freeIndex = i;
			POOL_LOG.slots--;
			break;
		}
	}
	return EXIT_SUCCESS;
}

uint8 
DBFreePool(pool_handle poolToFree)
{
	pool_handle PoolToFree = *((pool_handle*)POOL_LOG.begin + poolToFree->freeIndex);
	*((pool_handle*)POOL_LOG.begin + poolToFree->freeIndex) = NULL;

	PoolToFree->begin = LocalFree(PoolToFree->begin);
	PoolToFree->index.begin = LocalFree(PoolToFree->index.begin);
	if (PoolToFree->begin || PoolToFree->index.begin)
		return EXIT_POOLFREE_ERROR;

	PoolToFree->end = NULL;
	PoolToFree->index.end = NULL;
	PoolToFree->openSlots = 0;
	PoolToFree->voids = 0;
	PoolToFree->freeIndex = 0;

	return EXIT_SUCCESS;
}

uint8 
DBAllClear()
{
	if (!POOL_LOG.begin)
		return EXIT_NOPOOL_ERROR;

	pool_handle PoolToFree;
	uint32 LogSize = (POOL_LOG.end - POOL_LOG.begin) / sizeof(pool_handle*);
	for (uint32 i = 0; i < LogSize; i++)
	{
		if (*((pool_handle*)POOL_LOG.begin + i))
		{
			PoolToFree = *((pool_handle*)POOL_LOG.begin + i);
			PoolToFree->begin = LocalFree(PoolToFree->begin);
			PoolToFree->index.begin = LocalFree(PoolToFree->index.begin);
			if (PoolToFree->begin || PoolToFree->index.begin)
				return EXIT_POOLFREE_ERROR;

			PoolToFree->end = NULL;
			PoolToFree->index.end = NULL;
			PoolToFree->openSlots = 0;
			PoolToFree->voids = 0;
			PoolToFree->freeIndex = 0;
		}
	}
	POOL_LOG.begin = LocalFree(POOL_LOG.begin);
	POOL_LOG.end = NULL;
	POOL_LOG.slots = 0;

	return EXIT_SUCCESS;
}

uint8 
DBFindSpace(uint32 slotsRequired, uint32* openSlotIndex, pool_handle targetPool)
{
	if (slotsRequired > targetPool->openSlots)
		return EXIT_TOOFEWSLOTS_ERROR;

	uint64 TargetPoolSize = targetPool->end - targetPool->begin;
	uint32 ConsecutiveOpenSlots = 0;
	uint32 SlotsInPool = TargetPoolSize / SLOT_SIZE;
	if (targetPool->voids)
	{
		for (uint32 i = 0; i < SlotsInPool; i++)
		{
			if (*((uint32*)targetPool->begin + i) == INIT_VALUE)
				ConsecutiveOpenSlots++;
			if (*((uint32*)targetPool->begin + i) != INIT_VALUE)
				ConsecutiveOpenSlots = 0;
			if (ConsecutiveOpenSlots == slotsRequired)
			{
				*openSlotIndex = i - (ConsecutiveOpenSlots - 1);
				return EXIT_SUCCESS;
			}
		}
		//DEFRAG OR EXPANSION NECESSARY IF WE MAKE IT OUT OF THE LOOP
		//PERFORMANCE: EXPANSION->NOT POSSIBLE IN STATIC CASE
		//EFFICIENCY: DEFRAG->POSSIBLE IN STATIC CASE
	}
	*openSlotIndex = SlotsInPool - targetPool->openSlots;
	return EXIT_SUCCESS;
}

uint32
DBBlockSize(block_handle blockToCheck)
{
	uint32 BlockDataSize = blockToCheck->elementCount * blockToCheck->elementSize;
	uint32 BlockDataPtrBuf = blockToCheck->elementCount * sizeof(data_handle);
	uint32 BlockSize = BlockDataSize + BlockDataPtrBuf + sizeof(block);

	return BlockSize;
}

uint32
DBSlotCount(block_handle blockToCheck)
{
	uint32 BlockSize = DBBlockSize(blockToCheck);
	AlignToSize((size_t*)&BlockSize, SLOT_SIZE);
	uint32 SlotCount = BlockSize / SLOT_SIZE;

	return SlotCount;
}

uint8 
DB_Clear()
{
	
	return EXIT_SUCCESS;
}

uint8 
DB_Resize()
{
	
	return EXIT_SUCCESS;
}

uint8 
DB_Set(block_handle target, void* data, uint32 atElement)
{
	handle ElementAccess = *((data_handle)target->data + atElement);
	for (uint32 i = 0; i < target->elementSize; i++)
		*((char*)ElementAccess + i) = *((char*)data + i);

	return EXIT_SUCCESS;
}

uint8 
DB_PopAtLocation()
{
	
	return EXIT_SUCCESS;
}

uint8 
DB_Pop()
{
	
	return EXIT_SUCCESS;
}

uint8 
DB_PushAtLocation()
{
	
	return EXIT_SUCCESS;
}

uint8 
DB_Push()
{
	
	return EXIT_SUCCESS;
}

block_handle
DBBuildBlock(uint32 numElements, uint64 dataSize, pool_handle targetPool)
{
	uint64 RequiredSpace = (numElements * dataSize) + (numElements * sizeof(data_handle)) + sizeof(block);
	AlignToSize(&RequiredSpace, SLOT_SIZE);
	uint32 NumSlots = RequiredSpace / SLOT_SIZE;
	uint32 OpenSpaceIndex = 0;
	DBFindSpace(NumSlots, &OpenSpaceIndex, targetPool);

	block_handle TempBlockHandle = (block_handle)(targetPool->begin + (OpenSpaceIndex * SLOT_SIZE));
	(char*)TempBlockHandle->data = (char*)TempBlockHandle + sizeof(block);
	TempBlockHandle->elementCount = numElements;
	TempBlockHandle->elementSize = dataSize;
	TempBlockHandle->push = DB_Push;
	TempBlockHandle->push_at = DB_PushAtLocation;
	TempBlockHandle->pop = DB_Pop;
	TempBlockHandle->pop_at = DB_PopAtLocation;
	TempBlockHandle->set = DB_Set;
	TempBlockHandle->resize = DB_Resize;
	TempBlockHandle->clear = DB_Clear;
	
	handle FirstElement = TempBlockHandle->data + numElements;
	for(uint32 i = 0; i < numElements; i++)
	{
		*((data_handle)TempBlockHandle->data + i) = (handle)(FirstElement + (i * dataSize));
	}
	
	targetPool->openSlots -= NumSlots;
	return TempBlockHandle;
}
