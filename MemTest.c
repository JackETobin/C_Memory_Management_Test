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

/*
DBSETINITVALUES CHANGES THE DEFAULT SIZES THAT THIS SYSTEM 
USES WHEN BUILDING POOLS AND BLOCKS. DEFAULT VALUES ARE AS 
FOLLOWS:

->POOL SIZE:			1024MB
->POOL ALIGNMENT:		512

->SLOT SIZE:			64MB
->SLOT ALIGNMENT:		64

SET NEW VALUES BEFORE BUILDING POOLS AND BLOCKS, OTHERWISE 
THERE MAY BE UNPREDICTABLE BEHAVIOR. NOTE THAT IF A VALUE 
OF ZERO OR NULL IS PROVIDED FOR ANY OF THE ARGUMENTS, THAT 
SETTING IS IGNORED, AND SO DEFAULT SETTINGS ARE MAINTAINED.
*/
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
	{	//SET NEW POOL ALIGNMENT AND ALIGN POOL SIZE IF NEW SIZE ISNT PROVIDED.
		POOL_ALIGNMENT = newPoolAlignment;
		if(!newPoolSize)
			AlignToSize(&POOL_SIZE, POOL_ALIGNMENT);
	}
	if (newPoolSize)
	{	//SET NEW POOL SIZE THEN ALIGN.
		AlignToSize(&newPoolSize, POOL_ALIGNMENT);
		POOL_SIZE = newPoolSize;
	}
	if (newSlotAlignment)
	{	//SET NEW SLOT ALIGNMENT AND ALIGN SLOT SIZE IF NEW SIZE ISNT PROVIDED.
		SLOT_ALIGNMENT = newSlotAlignment;
		if (!newSlotSize)
			AlignToSize(&SLOT_SIZE, SLOT_ALIGNMENT);
	}
	if (newSlotSize)
	{	//CHECK TO SEE IF NEW SLOT SIZE IS LESS THAN EXISTING SLOT ALIGNMENT. 
		//IF SO SLOT ALIGNMENT MATCHES THE NEW SLOT SIZE.
		if (newSlotSize < SLOT_ALIGNMENT)
			SLOT_ALIGNMENT = newSlotSize;
		//ALIGNS NEW SLOT SIZE TO EXISTING ALIGNMENT AND SETS SIZE.
		AlignToSize(&newSlotSize, SLOT_ALIGNMENT);
		SLOT_SIZE = newSlotSize;
	}
}

/*
DBPOOLLOGALLOC IS CALLED AUTOMATICALLY TO FILL EXISTING LOG_POOL. THIS FUNCTION 
DOES NOT RETURN ANTTHING OTHER THAN A STATUS CODE, AND CANNOT BE USED TO BUILD 
A POOL. NOTE THAT THE DEFAULT ELEMENT ALLOWANCE CANNOT BE CHANGED IN 
DBSETINITVALUES; THIS IS BECAUSE THE POOL_LOG WILL GROW AUTOMATICALLY AS IS 
NECESSARY.
*/
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

/*
DBPOOLLOGEXPAND REALLOCATES THE POOL_LOG AND IS CALLED BY DBCREATEPOOL IN THE EVENT 
THAT THERE IS NOT ENOUGH ROOM IN THE POOL LOG TO STORE ANOTHER POOL_HANDLE.
*/
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

/*
DBCREATEPOOL TAKES A POOL_HANDLE AND RETURNS A STATUS CODE.
*/
uint8 
DBCreatePool(pool_handle poolToFill)
{	//CHECK TO SEE IF THE POOL_LOG HAS BEEN GENERATED, IF NOT: CALLS CONSTRUCTOR.
	if (!POOL_LOG.begin)
		DBPoolLogAlloc();

	//SETS POOL SIZE TO DEFAULT AND DOUBLE CHECKS ALIGNMENT.
	uint64 PoolSize = POOL_SIZE;
	AlignToSize(&PoolSize, POOL_ALIGNMENT);
	//CALCULATES NUMBER OF SLOTS TO BE PRESENT IN THE NEW POOL BASED ON SIZE.
	uint32 NumSlots = PoolSize / SLOT_SIZE;
	uint64 InitializationCounter = PoolSize / sizeof(uint32);
	uint64 BlockHandleIndexSize = (NumSlots) * sizeof(void*);

	//CALLS LOCALALLOC FROM WINDOWS TO FETCH A BLOCK IN MEMORY OF APPROPRIATE SIZE.
	poolToFill->begin = LocalAlloc(LMEM_FIXED, PoolSize);
	if (!poolToFill->begin)
		return EXIT_POOLALLOC_ERROR;

	//SETS THE END OF THE POOL TO THE LAST AVAILABLE ADDRESS ALLOCATED BY LOCALALLOC.
	poolToFill->end = poolToFill->begin + PoolSize;
	poolToFill->openSlots = NumSlots;

	//CYCLES THROUGH NEW MEMORY BLOCK AND INITIALIZES TO 0XFFFFFFFF. FINDSPACE FUNCTION 
	//USES MAX VALUE TO DETECT UNUSED SPACE.
	while (InitializationCounter--)
		*((uint32*)poolToFill->begin + InitializationCounter) = INIT_VALUE;
	
	//CHECKS FOR OPEN FREE SPACE IN THE POOL_LOG, IF NOT: CALLS EXPANSION FUNCTION.
	if (!POOL_LOG.slots)
		DBPoolLogExpand();
	
	//CALCULATES NUMBER OF SLOTS AVAILABLE IN THE POOL_LOG.
	uint32 LogSize = (POOL_LOG.end - POOL_LOG.begin) / sizeof(pool_handle*);
	//CHECKS FOR SPACE IN THE POOL_LOG WHERE THE ADDRESS OF THE POOL IS STORED.
	for (uint32 i = 0; i < LogSize; i++)
	{
		if (!*((pool_handle*)POOL_LOG.begin + i))
		{
			*((pool_handle*)POOL_LOG.begin + i) = poolToFill;
			//SETS FREE INDEX TO THE FREE SLOT FOUND IN THE POOL_LOG. FREE INDEX IS USED 
			//IN DBFREEPOOL TO REMOVE POOL ADDRESS FROM POOL_LOG.
			poolToFill->freeIndex = i;
			POOL_LOG.slots--;
			break;
		}
	}
	return EXIT_SUCCESS;
}

/*
DBDREEPOOL TAKES A POINTER TO THE POOL TO FREE AND RETURNS A STATUS CODE. THIS FUNCTION FREES 
A SINGLE POOL AND REMOVES THE POOL ADDRESS FROM THE POOL LOG SO THAT THERE ARE NO DOUBLE 
FREES WHEN DBALLCLEAR IS CALLED.
*/
uint8 
DBFreePool(pool_handle poolToFree)
{
	//USES FREE INDEX TO FIND THE ADDRESS OF THE POOL TO FREE IN THE POOL_LOG.
	pool_handle PoolToFree = *((pool_handle*)POOL_LOG.begin + poolToFree->freeIndex);
	//SETS THE CORRESPONDING ELEMENT IN THE POOL LOG TO NULL.
	*((pool_handle*)POOL_LOG.begin + poolToFree->freeIndex) = NULL;

	//CALLS LOCAL FREE ON THE DATA PORTION OF THE TARGET POOL, THEN CHECKS FOR SUCCESS.
	PoolToFree->begin = LocalFree(PoolToFree->begin);
	if (PoolToFree->begin)
		return EXIT_POOLFREE_ERROR;

	//SETS POOL END TO NULL POINTER AND ZEROS OUT THE REMAINING POOL DATA COMPONENTS.
	PoolToFree->end = NULL;
	PoolToFree->openSlots = 0;
	PoolToFree->voids = 0;
	PoolToFree->freeIndex = 0;

	return EXIT_SUCCESS;
}

/*
DBALLCLEAR IS TO BE CALLED AT THE END OF THE PROGRAM AND RETURNS A STATUS CODE. THIS FUNCTION 
CHECKS THE POOL_LOG FOR ANY EXISTING POOLS AND CALLS LOCALFREE FROM WINDOWS. ALL POOL ADDRESSES 
ARE AUTOMATICALLY ADDED TO THE POOL_LOG, AND THE FUNCTION FREES THE POOL_LOG AFTER ALL OF ITS 
ENTRIES HAVE BEEN FREED.
*/
uint8 
DBAllClear()
{	//CHECKS FOR THE EXISTANCE OF THE POOL_LOG.
	if (!POOL_LOG.begin)
		return EXIT_NOPOOL_ERROR;
	
	pool_handle PoolToFree;
	//CALCULATES THE NUMBER OF SLOTS IN THE POOL_LOG.
	uint32 LogSize = (POOL_LOG.end - POOL_LOG.begin) / sizeof(pool_handle*);
	//ITERATES THROUGH ALL SLOTS, CALLS LOCALFREE ON ANY NON-ZERO ENTRIES.
	for (uint32 i = 0; i < LogSize; i++)
	{
		if (*((pool_handle*)POOL_LOG.begin + i))
		{	//SETS POOLTOFREE TO THE ADDRESS EXISTING IN EMEMENT I OF THE POOL_LOG.
			PoolToFree = *((pool_handle*)POOL_LOG.begin + i);
			PoolToFree->begin = LocalFree(PoolToFree->begin);
			if (PoolToFree->begin)
				return EXIT_POOLFREE_ERROR;

			//ZEROS ALL DATA ASSOCIATED WITH THE POOL AT ELEMENT I OF THE POOL_LOG.
			PoolToFree->end = NULL;
			PoolToFree->openSlots = 0;
			PoolToFree->voids = 0;
			PoolToFree->freeIndex = 0;
		}
	}
	//CALLS LOCAL FREE ON THE POOL_LOG ITSELF AND ZEROS OUT ALL EXISTING DATA.
	POOL_LOG.begin = LocalFree(POOL_LOG.begin);
	POOL_LOG.end = NULL;
	POOL_LOG.slots = 0;

	return EXIT_SUCCESS;
}

/*
DBFINDSPACE TAKES THE NUMBER OF SLOTS REQUIRED FOR A DATA BLOCK, A POINTER TO 
AN INT THAT IS USED TO STORE AN INDEX IN THE TARGET POOL THAT DENOTES THE 
BEGINNING OF SPACE LARGE ENOUGH TO FIT DATA, AND THE TARGET POOL. tHIS FUNCTION 
IS USED WHEN BUILDING DATA BLOCKS TO CHECK THE POOL FOR FREE SPACE IN WHICH THE 
DATA CAN BE STORED. IN THE FUTURE THERE MIGHT BE A DEFRAG FUNCTION IMPLEMENTED 
FOR THE CASE WHERE THERE ARE ENOUGH BLOCKS OPEN THROUGHOUT MEMORY, BUT THOSE 
BLOCKS ARE NOT CONTIGUOUS. DATA CAN ONLY BE STORED IN CONTIGUOUS MEMORY IN THIS 
SYSTEM.
*/
uint8 
DBFindSpace(uint32 slotsRequired, uint32* openSlotIndex, pool_handle targetPool)
{
	//CHECKS NUMBER OF OPEN SLOTS IN THE TARGET POOL.
	if (slotsRequired > targetPool->openSlots)
		return EXIT_TOOFEWSLOTS_ERROR;

	//CALCULATES TOTAL NUMBER OF SLOTS IN POOL.
	uint64 TargetPoolSize = targetPool->end - targetPool->begin;
	uint32 ConsecutiveOpenSlots = 0;
	uint32 SlotsInPool = TargetPoolSize / SLOT_SIZE;
	handle SlotCheck;
	
	//ITERATES THROUGH ALL SLOTS IN POOL LOOKING FOR THOSE THAT ARE EMPTY.
	for (uint32 i = 0; i < SlotsInPool; i++)
	{
		//IF A SLOT IS AVAILABLE IT IS ADDED TO A CONTIGUOUS SLOT COUNTER.
		SlotCheck = targetPool->begin + (i * SLOT_SIZE);
		if (*((uint32*)SlotCheck) == INIT_VALUE)
			ConsecutiveOpenSlots++;
		if (*((uint32*)SlotCheck) != INIT_VALUE)
			ConsecutiveOpenSlots = 0;

		//IF THE CORRECT NUMBER OF CONTIGUOUS SLOTS ARE FOUND, SETS INDEX 
		//TO THE BEGINNING OF THE CONTIGUOUS SLOTS FOR DATA STORAGE.
		if (ConsecutiveOpenSlots == slotsRequired)
		{
			*openSlotIndex = i - (ConsecutiveOpenSlots - 1);
			return EXIT_SUCCESS;
		}
	}
	//DEFRAG OR EXPANSION NECESSARY IF WE MAKE IT OUT OF THE LOOP
	//PERFORMANCE: EXPANSION->NOT POSSIBLE IN STATIC CASE
	//EFFICIENCY: DEFRAG->POSSIBLE IN STATIC CASE
	
	//IF NO CONTIGUOUS SPACE IS FOUND, RETURN MAX VALUE.
		*openSlotIndex = INIT_VALUE;
	return EXIT_POOLNOSPACE_ERROR;
}

/*
DBBLOCKSIZE TAKES A POINTER TO A TARGET BLOCK AND RETURNS THE TOTAL SIZE 
OF THAT BLOCK, INCLUDING THE SPACE USED FOR METADATA.
*/
uint32
DBBlockSize(block_handle blockToCheck)
{
	uint32 BlockDataSize = blockToCheck->elementCount * blockToCheck->elementSize;
	uint32 BlockDataPtrBuf = blockToCheck->elementCount * sizeof(data_handle);
	uint32 BlockSize = BlockDataSize + BlockDataPtrBuf + sizeof(block);

	return BlockSize;
}

/*
DBSLOTCOUNT TAKES A POINTER TO A TARGET BLOCK AND RETURNS THE TOTAL NUMBER 
OF SLOTS IN THE POOL THAT THE BLOCK OCCUPIES. THIS FUNCTION CALLS DBBLOCKSIZE 
FOR THE TOTAL SIZE OF THE BLOCK AND USES THE SLOT SIZE VALUE FOR CALCULATION.
*/
uint32
DBSlotCount(block_handle blockToCheck)
{
	uint32 BlockSize = DBBlockSize(blockToCheck);
	AlignToSize((size_t*)&BlockSize, SLOT_SIZE);
	uint32 SlotCount = BlockSize / SLOT_SIZE;

	return SlotCount;
}


/*
DBRELEASE TAKES A DOUBLE POINTER TO A TARGET BLOCK AND IS CALLED VIA POINTER 
FROM THE BLOCK ITSELF. THIS FUNCTION REINITIALIZES A TARGET BLOCK TO 0XFFFFFFFF, 
MAKING IT DETECTABLE BY DBFINDSPACE FOR STORAGE OF NEW DATA.
*/
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

/*
DBRESIZE TAKES A DOUBLE POINTER TO A TARGET BLOCK AND A NEW NUMBER OF ELEMENTS 
FOR THE TARGET BLOCK TO BE RESIZED TO. THIS FUNCTION IS CALLED VIA POINTER BY THE 
BLOCK ITSELF. THIS FUNCTION CALLS DBBUILD BLOCK USING THE INFORMATION PRESENT ON 
THE BLOCK ITSELF AND REALLOCATES SPACE ON THE POOL FOR THE DATA. AFTER 
REALLOCATING SPACE AND COPYING EXISTING DATA OVER TO THE NEW STRUCTURE, DBRELEASE 
IS CALLED AND THE SPACE DEDICATED TO THE OLD STRUCTURE IS MADE AVAILABLE FOR DATA 
STORAGE. THE TARGET BLOCK WILL POINT TO THE ADDRESS OF THE NEW STRUCTURE AFTER 
THE RESIZE.
*/
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

uint8 
DB_Set(block_handle target, void* data, uint32 atElement)
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
