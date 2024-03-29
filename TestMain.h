/* 
WRITTEN BY JACK UNDER AN MIT LICENSE.

SHOULD THIS PROGRAM BE USEFUL TO ANYONE IN ANY CAPACITY, PLEASE 
FEEL FREE TO USE IT. MY GOAL IS TO LEARN AND ENJOY THE C 
PROGRAMMING LANGUAGE, AND TO BE ABLE TO HELP SOMEONE ELSE ALONG 
THE WAY WOULD BE A VERY WELCOME BYPRODUCT OF MY EFFORTS. IF 
ANYONE HAS ANY HELPFUL POINTERS OR INSIGHTS, I AM MORE THAN HAPPY 
TO HEAR FROM YOU, SO FEEL FREE TO COMMENT ON MY GITHUB REPOSITORY.

CONSTRUCTIVE CRITICISM IS MORE THAN WELCOME!

THIS IS A DYNAMIC MEMORY MANAGEMNT SYSTEM THAT USES ONE OR 
MULTIPLE MEMORY POOLS FOR DATA STORAGE IN C. NOTE THAT AS OF 
RIGHT NOW THIS OPERATES VIA WINDOWS.H AND WILL ONLY WORK ON A 
WINDOWS OPERATING SYSTEM.

THE PROGRAM IS DESIGNED TO FUNCTION SUCH THAT LARGE POOLS ARE 
ALLOCATED AT THE START OF A PROGRAM AND ARE BROKEN UP BY SLOTS. 

POOLS ARE ALLOCATED AS REQUESTED VIA THE DBCREATEPOOL FUNCTION.
AS OF RIGHT NOW POOL DATA IS NOT STORED AT THE BEGINING OF THE 
POOL AS METADATA, BUT THAT MIGHT CHANGE IN THE NEAR FUTURE. FOR 
NOW, POOLS ARE ACCESSED VIA THE POOL_STATIC TYPE. NOTE THAT WHEN 
BUILDING BLOCKS TO FIT INTO THE DATA POOLS, THE POOL INTENDED 
FOR STORAGE MUST BE INDICATED IN THE DBBUILDBLOCK FUNCTION, SO 
PLEASE KEEP TRACK OF WHICH POOLS ARE WHERE. UPON CREATING A POOL 
A POINTER TO THAT POOL WILL BE STORED IN A SEPARATE POOL CALLED 
THE POOL_LOG BY DEFAULT. SHOULD ANY POOLS BE CREATED, THE 
DBALLCLEAR FUNCTION SHOULD BE CALLED AT THE END OF THE PROGRAM. 
DBALLCLEAR FREES ALL POOLS STORED IN THE POOL_LOG AND THE 
POOL_LOG ITSELF.

BLOCKS ARE USED TO STORE DATA AND ARE ALIGNED WITH THE SLOTS. 
BLOCKBODY STRUCTS ARE ALLOCATED ON THE POOL ITSELF BEFORE THE 
DATA. BLOCK_HANDLE POINTERS ARE USED TO ACCESS BOTH METADATA 
AND DATA ITSELF. DATA_HANDLE POINTERS ARE DOUBLE POINTERS USED 
TO ACCESS DATA. DATA IS STORED AS A DOUBLE POINTER THAT POINTS 
TO A MEMORY LOCATION THAT EXISTS JUST AFTER THE DOUBLE POINTER 
LIST. DOUBLE POINTERS AND DATA ELEMENTS CORRESPOND WITHT THE 
ELEMENT COUNT LISTED IN THE METADATA.

BLOCK FUNCTIONALITY IS AS FOLLOWS:
(B)->SET:	SET TAKES A TARGET BLOCK, DATA THAT IS TO BE STORED 
		IN THE POOL, AND THE BLOCK ELEMENT AT WHICH THE DATA 
		CAN BE FOUND. SET WILL OVERWRITE EXISTING DATA AT ANY 
		ELEMENT, AND WILL ONLY WRITE UP TO THE SIZE OF THE 
		ELEMENT WHICH WOULD HAVE BEEN PROVIDED UPON CREATING 
		THE BLOCK.

(B)->SET_SHIFT: SET_SHIFT TAKES A TARGET BLOCK, AND DATA THAT IS TO BE
		STORED IN THE POOL. NEW DATA IS WRITTEN IN THE FIRST 
		ELEMENT OF THE BLOCK, AND ALL ELEMENTS ARE PUSHED 
		BACKWARD, OVERWRITING THE LAST ELEMENT.

(B)->DELETE:	DELETE TAKES A TARGET BLOCK, AND AN ELEMENT FROM WHICH 
		DATA IS TO BE REMOVED. ELEMENTS EXISTING AFTER THE 
		DELETED ELEMENT WILL BE PUSHED FORWARD TO FILL THE VOID, 
		AND THE LAST ELEMENT WILL BE REINITIALIZED TO ZERO.

(B)->RESIZE:	RESIZE TAKES A TARGET BLOCK ADDRESS, AND A NEW NUMBER OF 
		ELEMENTS TO STORE. THIS MOVES THE BLOCK POINTER TO A NEW 
		ADDRESS AS A NEW BLOCK THAT IS OF APPROPRIATE SIZE, AND 
		COPIES ALL EXISTING DATA OVER TO THE NEW ADDRESS. THE OLD 
		ADDRESS IS REINITIALIZED TO 0XFFFFFFFF SO THAT THE SPACE 
		FINDER CAN LOCATE AND REALLOCATE BLOCKS IN THE VOID. NOTE 
		THAT IF THE NEW SIZE IS SMALLER THAN THE PREVIOUS SIZE, 
		THE FUNCTIONALITY REMAINS THE SAME, BUT THE DATA COPIED 
		WILL BE TRUNCATED AT THE NEW ELEMENT LIMIT.

(B)->RELEASE:	RELEASE TAKES A TARGET BLOCK ADDRESS. MEMORY SPACE 
		DEDICATED TO THE TARGET BLOCK WILL BE REINITIALIZED TO 
		0XFFFFFFFF AND THE BLOCK'S POINTER WILL BE SET TO NULL. 
		USING THE BLOCK AGAIN WILL REQUIRE A CALL TO THE 
		DBBUILDBLOCK FUNCTION.

CHECK NOTATION ABOVE FUNCTION DEFINITIONS IN MEMTEST.C FOR MORE DETAILED 
EXPLANATIONS OF HOW EACH FUNCTION WORKS.

ENJOY!
*/


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

/*
DEFINED AHEAD ARE THE DEFAULT VALUES ASSOCIATED WITH THE POOL 
ALLOCATION SIZE, SLOT SIZE AND RESPECTIVE ALIGNMENTS. THESE 
VALUES CAN BE CHANGED BY CALLING THE DBSETIITVALUES FUNCTION, 
OR CHANGED DIRECTLY. NOTE THAT THIS WORKS BEST WHEN ALL VALUES 
ARE IN SOME WAY DIVISIBLE BY 8. FUNCTIONALITY IMPROVES AS ALIGNMENT 
SIZE INCREASES.
*/
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

/*
DEFINED AHEAD ARE ERROR CODES THAT I USE DOMESTICALLY IN AN ERROR 
CATCH. THE ERROR CATCH IS NOT IMPLEMENTED HERE, AND I CURRENTLY HAVE 
NO INTENTION OF IMPLEMENTING IT HERE, BUT I WANT TO MAKE THESE AVAILABLE 
FOR ANYONE WHO MIGHT FIND THEM CONVENIENT.
*/
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
	uint8(*set_shift)(struct BlockBody* target, void* data);
	uint8(*delete)(struct BlockBody* target, uint32 elementToDelete);
	uint8(*resize)(struct BlockBody** target, uint32 newNumElements);
	void(*release)(struct BlockBody** target);
}block, *block_handle;

/*
CALL DBSETINITVALUES TO SET THE SIZE OF THE POOL, SLOTS, AND ALIGNMENTS 
RESPECTIVELY. CALL BEFORE POOL CREATION, MISALIGNED POOLS AND BLOCKS MIGHT 
CAUSE UNPREDICTABLE BEHAVIOR. NOTE THAT THE INITIAL POOL SIZE IS SET TO BE 
1KB, AND THAT ALL SIZES AND ALIGNMENTS ARE DIVISIBLE BY 8.
*/
void
DBSetInitValues
(
	uint64			newSlotSize,
	uint64			newSlotAlignment,
	uint64			newPoolSize,
	uint64			newPoolAlignment
);

/*
DBCREATEPOOL TAKES A POINTER TO A POOL_STATIC, POOL METADATA IS NOT STORED IN POOL 
ITSELF, YET.
*/
uint8
DBCreatePool
(	
	pool_handle		poolToFill
);

/*
DBFREEPOOL TAKES A POINTER TO A POOL_STATIC AND FREES THE MEMORY ASSOCIATED WITH 
THE POOL. IT ALSO REMOVES THE POOL FROM THE POOL_LOG THAT THE DBALLCLEAR FUNCTION 
FREES FROM SO THAT THERE ARE NO DOUBLE FREES. NOTE THAT THIS FUNCTION ONLY FREES
THE SINGLE POOL THAT THE POOL_HANDLE POINTS TO.
*/
uint8
DBFreePool
(
	pool_handle		poolToFree
);

/*
DBALLCLEAR FREES ALL EXISTING POOLS.
*/
uint8
DBAllClear();

/*
DBBLOCKSIZETAKES A BLOCK_HANDLE AND RETURNS THE TOTAL SIZE OF THE BLOCK THAT THE 
HANDLE IS ASSOCIATED WITH. NOTE THAT THE RETURNED SIZE IS THE TOTAL SIZE ON MEMORY 
THAT THE BLOCK OCCUPIES, INCLUDING METADATA, POINTERS, AND ACTUAL DATA.
*/
uint32
DBBlockSize
(
	block_handle	blockToCheck
);

/*
DBSLOTCOUNT TAKES A BLOCK_HANDLE AND RETURNS THE NUMBER OF SLOTS THAT THE BLOCK 
OCCUPIES IN ITS DESIGNATED POOL.
*/
uint32
DBSlotCount
(
	block_handle	blockToCheck
);

/*
DBBUILDBLOCK TAKES AN INTEGER THAT DESIGNATES THE NUMBER OF ELEMENTS THE DESIRED BLOCK 
SHOULD HAVE, AND INTEGER THAT DESIGNATES THE SIZE OF EACH ELEMENT, AND A POINTER TO A 
POOL_STATIC THAT DESIGNATES WHICH POOL THIS BLOCK IS TO EXIST IN. IT RETURNS A BLOCK 
HANDLE TO A NEWLY INITIALIZED BLOCK NOTE THAT THIS FUNCTION SETS THE POINTERS TO THE 
BLOCK FUNCTIONS, AND BECAUSE OF THAT THIS FUNCTION MUST BE CALLED BEFORE ANY BLOCK CAN 
BE USED.
*/
block_handle
DBBuildBlock
(
	uint32			numElements,
	uint64			dataSize,
	pool_handle		targetPool
);
#endif
