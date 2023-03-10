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
	handle SlotCheck;
	
	for (uint32 i = 0; i < SlotsInPool; i++)
	{
		SlotCheck = targetPool->begin + (i * SLOT_SIZE);
		if (*((uint32*)SlotCheck) == INIT_VALUE)
			ConsecutiveOpenSlots++;
		if (*((uint32*)SlotCheck) != INIT_VALUE)
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
	
		*openSlotIndex = INIT_VALUE;
	return EXIT_POOLNOSPACE_ERROR;
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
void 
DB_Release(block_handle* target)
{
	(*target)->inPool->openSlots += (*target)->elementCount;
	uint64 ReleaseSize = (DBSlotCount(*target) * SLOT_SIZE);

	block_handle tempAccess = *target;
	*target = NULL;
	if (ReleaseSize % sizeof(uint32) == 0)
	{
		ReleaseSize = ReleaseSize / sizeof(uint32);
		while (ReleaseSize--)
			*((uint32*)tempAccess + ReleaseSize) = INIT_VALUE;
	}
	else
	{
		unsigned char Release = 0xFF;
		while(ReleaseSize--)
			*((unsigned char*)tempAccess + ReleaseSize) = Release;
	}
	//return EXIT_SUCCESS;
}

uint8 
DB_Resize(block_handle* target, uint32 newNumElements)
{	
	block_handle NewHandle = DBBuildBlock(newNumElements, (*target)->elementSize, (*target)->inPool);
	if (NewHandle->elementCount > (*target)->elementCount)
	{
		for (uint32 i = 0; i < (*target)->elementCount; i++)
			NewHandle->set(NewHandle, *((data_handle)(*target)->data + i), i);
	}
	if (NewHandle->elementCount < (*target)->elementCount)
	{
		for (uint32 i = 0; i < NewHandle->elementCount; i++)
			NewHandle->set(NewHandle, *((data_handle)(*target)->data + i), i);
	}

	(*target)->release(target);
	*target = NewHandle;
	if (!(*target))
		return EXIT_BLOCKRESIZE_ERROR;

	return EXIT_SUCCESS;
}

uint8 
DB_Delete(block_handle target, uint32 elementToDelete)
{
	if ((elementToDelete) < target->elementCount);
	{
		uint64 ShiftSize = (target->elementCount - (elementToDelete)) * target->elementSize;
		handle ElementToDelete = *((data_handle)target->data + elementToDelete);
		for (uint64 i = 0; i < ShiftSize; i++)
			*((char*)ElementToDelete + i) = *((char*)ElementToDelete + target->elementSize + i);
	}
	handle LastElement = *((data_handle)target->data + (target->elementCount - 1));
		for (uint64 i = 0; i < target->elementSize; i++)
			*((char*)LastElement + i) = 0;
}

uint8 
DB_Set_Shift(block_handle target, void* data)
{
	if (target->elementCount > 1)
	{
		uint64 ShiftSize = (target->elementCount - 1) * target->elementSize;
		handle ElementToDelete = *((data_handle)target->data);
		for (uint64 i = 0; i < ShiftSize; i++)
			*((char*)ElementToDelete + i) = *((char*)ElementToDelete + target->elementSize + i);
	}	
	handle ElementAccess = *((data_handle)target->data + (target->elementCount - 1));
	for (uint32 i = 0; i < target->elementSize; i++)
		*((char*)ElementAccess + i) = *((char*)data + i);

	return EXIT_SUCCESS;
}

uint8 DB_Set(block_handle target, void* data, uint32 atElement)
{
	if (atElement > (target->elementCount - 1))
		return EXIT_OUTOFRANGE_ERROR;

	handle ElementAccess = *((data_handle)target->data + atElement);
	for (uint32 i = 0; i < target->elementSize; i++)
		*((char*)ElementAccess + i) = *((char*)data + i);

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
	for (uint32 i = 0; i < NumSlots; i++)
		*(char*)(targetPool->begin + (OpenSpaceIndex * SLOT_SIZE) + (i * SLOT_SIZE)) -= 1;	

	block_handle TempBlockHandle = (block_handle)(targetPool->begin + (OpenSpaceIndex * SLOT_SIZE));
	TempBlockHandle->data = (char*)TempBlockHandle + sizeof(block);
	TempBlockHandle->elementCount = numElements;
	TempBlockHandle->elementSize = dataSize;
	TempBlockHandle->inPool = targetPool;
	TempBlockHandle->set = DB_Set;
	TempBlockHandle->set_shift = DB_Set_Shift;
	TempBlockHandle->delete = DB_Delete;
	TempBlockHandle->resize = DB_Resize;
	TempBlockHandle->release = DB_Release;
	
	handle FirstElement = TempBlockHandle->data + numElements;
	for(uint32 i = 0; i < numElements; i++)
		*((data_handle)TempBlockHandle->data + i) = (handle)(FirstElement + (i * dataSize));
	
	targetPool->openSlots -= NumSlots;
	return TempBlockHandle;
}
