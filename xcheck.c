#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/stat.h>

#include "types_defs.h"
#include "fs_defs.h"

// Constants
#define BLOCK_SIZE 512
#define ROOT_INO 1
#define INODE_PB (BLOCK_SIZE / sizeof(struct dinode))

#define T_UNALLOC 0
#define T_DIR 1
#define T_FILE 2
#define T_DEV 3

// Data structure for on-disk block
struct block {
	char* data;
};                 

// Analysis prototypes
int bitmapInInodesTest();
int inodesInBitmapTest();

// Basic utility prototypes
int inodeInBitmap(struct dinode*);
int blockInUse(int);
int useableType(int);
int validInode(struct dinode*);
int blockBit(int);
void bread(int, struct block*);
void init(char*);
int inode2Block(int);

// Debug prototypes
void debugPrintByte(char);
void debugDumpBlock(struct block, int);
void int2Binary(int, char[8]);
void cleanup();

// File descriptor of file system
int FSFD;

// Number of offset blocks to access the data block region
int DATA_OFFSET;

// The beginning address for the file system mapped into
// the application
char* FS_ADDR;

// Super block of the file system
struct superblock* SUPER_BLOCK;

// Points to inodes in the file system
struct dinode* INODES;

// Points to the root directory entry
struct dirent* ROOT_DIR;

// The bitmap for which data blocks have been used
char* BMAP;

int main (int argc, char *argv[]){
	// Check for valid arguments
	if(argc < 2){
		fprintf(stderr, "Usage: xcheck <file_system_image>\n"); 
		exit(1);
	}

	// Initialize the file system in the application
	init(argv[1]);

	printf("Super block: %u\n", SUPER_BLOCK->size);

	/*int i, j;
	for(i = 0, j=1; i < SUPER_BLOCK->ninodes; i++){
		if(INODES[i].size != 0){
			printf("Inode %d: %u\n", j, INODES[i].size);
			j++;
		}
	}*/

	int i;
	for(i = 0; i < INODES[1].size / sizeof(struct dirent); i++){
		struct dirent dir = ROOT_DIR[i];
		printf("[%d] [%s]\n", dir.inum, dir.name);
	}

/*	uint* addr = INODES[1].addrs;
	int j;
	for(j = 0; j < 13; j++){
		printf("[%u]\n", addr[j]);
	}
*/
//	printf("[%u]\n", addr[0]);
//	struct block b;
//	b.data = BMAP;

//	debugDumpBlock(b, 100);

/*	int k;
	for(k = 0; k < SUPER_BLOCK->ninodes; k++){
		validInode(INODES[k]);
	}
*/
	if(!inodesInBitmapTest()){
		printf("ERROR: address used by inode marked free in bitmap.\n");
	}
	
	if(!bitmapInInodesTest()){
		printf("ERROR: bitmap marks block in use but it is not in use.\n");
	}
	printf("%d\n", DATA_OFFSET);
//	int k = blockBit(408);
//	printf("{%d}\n", k);

//	debugPrintByte(BMAP[51]);

	cleanup();
	exit(0);
}

// ***
// *
// *   Analysis functions
// *
// ***

int bitmapInInodesTest(){
	int i;
	for(i = 0; i < SUPER_BLOCK->size; i++){

	}

	return 1;	
}

// Returns 1 if for all in-use inodes, each block in use is also marked in-use by the
// bitmap. Returns 0 if an inode is using a block which is not marked as in-use by the
// bitmap.
int inodesInBitmapTest(){
	// Iterate through all the inodes
	int i;
	for(i = 0; i < SUPER_BLOCK->ninodes; i++){
		// If the inode isn't usable, don't examine it
		if(useableType(INODES[i].type)){
			printf("Inode: %d\n", i);
			// If the inode's data blocks aren't marked as in-use by the bitmap, 
			// return false
			if(!inodeInBitmap(&INODES[i]))
				return 0;
		}
	}
	
	// All inode data blocks are properly documented in the bitmap. This test
	// has been passed, so return true
	return 1;
}

// ***
// *
// *   Utility functions
// *
// ***

// Checks if the data blocks referenced by a given inode are set as
// in-use within the bitmap
int inodeInBitmap(struct dinode* inode){
	// Lits of block indexes referenced by the inode
	uint* refBlocks = inode->addrs;

	// Check the linked data blocks within the inode - all the directly linked blocks,
	// and the single linked block at the end
	int i;
	for(i = 0; i < NDIRECT + 1; i++){
		int bIndex = refBlocks[i];
		// If the direct link hasn't been instantiated yet, check
		// the next one
		if(bIndex == 0)
			continue;
		printf("  [%d]\n", bIndex);		
		// Block index is negative when it is writing
		// sequential data from higher blocks to lower blocks.
		// Realign the index to positive.
		if(bIndex < 0)
			bIndex = bIndex * -1;

		// If the current directly linked block isn't in use in the bitmap, but it's referenced
		// by the inode, return false
		if(!blockInUse(refBlocks[i]))
			return 0;
	}

	// Check indirectly linked data blocks referenced at the end, if they exist
	if(refBlocks[NDIRECT] != 0){
		// Read the block which stores the indirect links
		struct block b;
		bread(refBlocks[NDIRECT], &b);
		printf("     {%d}\n", refBlocks[NDIRECT]);
		// Iterate through the list of blocks stored in the indirect block
		uint* indirect = (uint*)b.data;

		int readLength = (inode->size - (NDIRECT * BLOCK_SIZE)) / BLOCK_SIZE;
		printf("<%d>\n", readLength);
		for(i = 0; i < readLength + 1; i++){
			// Get the current block index from the indirect block
			int bIndex = indirect[i];
		printf("        (%d)\n", bIndex);
			// Check if its used by the inode
			if(bIndex == 0)
				continue;
			
			//debugPrintByte(indirect[i]);
			// Block index is negative when it is writing
			// sequential data from higher blocks to lower blocks.	
			// Realign the index to positive.                     		
			//if(bIndex < 0)
			//	bIndex = bIndex * -1;
		
			// If the current indirectly linked blocked isn't in use in the bitmap, but its
			// referenced by the inode, return flase
			if(!blockInUse(bIndex))
				return 0;
		}
	}

	// The blocks referenced by the inodes and the bitmap match up - return true;
	return 1;
}

// Examines if the block at block index is marked as "in use" by the bitmap
int blockInUse(int blockIndex){
	// If we're trying to access an invalid block, return false
	if(blockIndex < 0)
		return 0;

	if(blockIndex > SUPER_BLOCK->nblocks - 1)
		return 0;
	
	// If the bit is '0' at the index, then the block is not in use
	if(!blockBit(blockIndex))
		return 0;

	// Otherwise, assume the block is in use, and return true
	return 1;
}

// Checks if the type supplied is usable in the application
// xv6 file systems only support 3 explicit serial types, so we only
// need to check within a range
int useableType(int type){
	if(type > 1 && type < 4)
		return 1;

	return 0;
}

// Checks if the inode given to it is valid
int validInode(struct dinode* inode){
	// If the type of the inode isn't recognized as immediately useable,
	// examine it more
	if(!useableType(inode->type)){
		// If the inode is just unallocated, return true
		if(inode->type == T_UNALLOC)
			return 1;

		// Otherwise, return false
		return 0;
	}
	
	// If the type is immediately useable, return true;
	return 1;
}

// Gets the bit from the bitmap at the given index
int blockBit(int index){
	// If we the desired block index is unaccesible, return 0
	if(index < 1 || index > SUPER_BLOCK->nblocks)
		return 0;

	// Bit we will return
	int bit;

	// Get the byte the bit is in
	int byte = index / 8;

	// Get the position within the byte the index references
	int bitPos = index % 8;
	
	// Shift and mask the bits to get the one we want
	char raw = BMAP[byte];
	raw = raw >> bitPos;
	raw = raw & 0x1;

	// Convert and return our bit in a more useable state
	bit = (int)raw;
	return bit;
}

// Reads the block data at position index into a block structure
void bread(int index, struct block* b){
	b->data = &FS_ADDR[index * BLOCK_SIZE];
}

// Init prerequisite data and structures before
// filesystem analysis begins
void init(char* fileName){
	// Get the file descriptor to the file system
	FSFD = open(fileName, O_RDONLY);

	// If there was an error, output a message and exit
	if(FSFD < 0){
		fprintf(stderr, "Error opening file system!\n");
		exit(1);
	}

	// Get info on the file system
	struct stat finfo;
	if(fstat(FSFD, &finfo) < 0){
		fprintf(stderr, "Error loading file system info!\n");
		exit(1);
	}

	// Create an address mapping to the entire file system
	int fsize = finfo.st_size;
	FS_ADDR = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, FSFD, 0);
	if(FS_ADDR == MAP_FAILED){
		fprintf(stderr, "Error mapping file system into memory!");
	}

	// Read the super block
	struct block b;
	bread(1, &b);
	SUPER_BLOCK = (struct superblock*) b.data;

	// Read the inodes block
	bread(2, &b);
	INODES = (struct dinode*)b.data;

	// Read the root directory
	bread(INODES[ROOT_INO].addrs[0], &b);
	ROOT_DIR = (struct dirent*)b.data;

	// Read the used data block bitmap
	int bmBlock = inode2Block(SUPER_BLOCK->ninodes) + 1;
	bread(bmBlock, &b);
	BMAP = b.data;

	// Get the offset for data blocks
	DATA_OFFSET = bmBlock + 1;	
}

// Returns the block which an inode at inodeIndex is located in
int inode2Block(int inodeIndex){
	return (inodeIndex / INODE_PB) + 2;
}

// ***
// *
// *   Debug Functions
// *
// ***

// Prints the individuals bits of a byte to the console
void debugPrintByte(char byte){
	int i;
	for(i = 0; i < 8; i++){
		printf("%d", ((byte >> (7 - i)) & 0x1));
	}
	printf("\n");
}

// Dumps a block's data to the command line, with MAX to control 
// amount of data dumped                                         
void debugDumpBlock(struct block b, int MAX){
	int i;
	for(i = 0; i < MAX; i++){
		printf("[%d]\n", b.data[i]);
	}
}

// Cleanup the application
void cleanup(){
	//free(INODES);
	//free(SUPER_BLOCK);
	close(FSFD);
}
