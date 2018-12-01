#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
int indirectAddressTest();
int directAddressTest();
int directoryTest();
int rootTest();
int inodesValidTest();
int inodesAddressTest();
int bitmapInInodesTest();
int inodesInBitmapTest();

// Basic utility prototypes
int uniqueAddr(uint*, int);
int dirCheck(uint, uint);
int validDirect(struct dinode*, uint);
int validAddresses(struct dinode*);
int blockInInodes(int);
int inodeInBitmap(struct dinode*);
int readLength(int);
int blockInUse(int);
int useableType(int);
int validInode(struct dinode*);
int blockBit(int);
void bread(int, struct block*);
void init(char*);
int inode2Block(int);

// Debug prototypes
void debugDumpDir(struct dinode*);
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

	// Debugging block
	/*
	printf("Should output 1024: %u\n", SUPER_BLOCK->size);
	debugDumpDir(&INODES[1]);
	debugPrintByte(BMAP[51]);
	printf("%d\n", DATA_OFFSET);
	*/

	// Run tests
	if(!inodesValidTest()){
		printf("ERROR: bad inode\n");
		exit(1);
	}
	
	if(!inodesAddressTest()){
		exit(1);
	}

	if(!rootTest()){
		printf("ERROR: root directory does not exit.\n");
		exit(1);
	}

	if(!directoryTest()){
		printf("ERROR: directory not properly formatted.\n");
		exit(1);
	}

	if(!inodesInBitmapTest()){
		printf("ERROR: address used by inode marked free in bitmap.\n");
		exit(1);
	}
	
	if(!bitmapInInodesTest()){
		printf("ERROR: bitmap marks block in use but it is not in use.\n");
		exit(1);
	}

	if(!directAddressTest()){
		printf("ERROR: direct address used more than once.\n");
		exit(1);
	}

	if(!indirectAddressTest()){
		printf("ERROR: indirect address used more than once.\n");
		exit(1);
	}

	printf("Check complete!\n");

	cleanup();
	exit(0);
}

// ***
// *
// *   Analysis functions
// *
// ***

// Checks that all indirect addresses for in-use inodes are only referenced once within the redirect block of the inode.
int indirectAddressTest(){
	// Iterate through the inodes
	int i;
	for(i = 0; i < SUPER_BLOCK->ninodes; i++){
		// If the inode is usable, examine it further
		if(useableType(INODES[i].type)){
			// Get the referenced blocks in the inode
			uint* refBlocks = INODES[i].addrs;

			// If the indirect block is unallocated, then don't worry about it
			if(refBlocks[NDIRECT] == 0)
				continue;

			// Read the indirect block from the file system
			struct block b;
			bread(refBlocks[NDIRECT], &b);
			uint* indirect = (uint*)b.data;

			// If the indirect block has repeated addresses within it, then return false
			if(!uniqueAddr(indirect, readLength(INODES[i].size))){
				return 0;
			}
		}
	}

	// Otherwise, return 1
	return 1;
}

// Checks that all direct addresses for in-use inodes are only referenced once by each inode.
// Links to the same blocks by other inodes allowed, in the event of hard linking on the file system
int directAddressTest(){
	// Iterate through the inodes
	int i;
	for(i = 0; i < SUPER_BLOCK->ninodes; i++){
		// If the inode is usable, examine it further
		if(useableType(INODES[i].type)){
			// If the inode has non-unique block references, return false.
			uint* refBlocks = INODES[i].addrs;
			if(!uniqueAddr(refBlocks, NDIRECT + 1))
				return 0;
		}
	}

	// No repeated block references detected - return true
	return 1;
}

// Examines all directories, and determines that they are properly formatted
int directoryTest(){
	// Iterate through inodes
	int i;
	for(i = 0; i < SUPER_BLOCK->ninodes; i++){
		// If the inode is a directory, examine it further
		if(INODES[i].type == T_DIR){
			// If not a valid directory, return false
			if(!validDirect(&INODES[i], i))
				return 0;
		}
	}

	// All directories are valid - return true
	return 1;
}

// Examines the root directory, and returns 1 if it's data is correct. Return 0 otherwise.
int rootTest(){
	// / Realod the root inode based on expected data
	struct dinode rootInode = INODES[ROOT_INO];

	// Make sure the root inode is usable
	if(!useableType(rootInode.type))
		return 0;

	// The root shouldn't be empty
	if(rootInode.size == 0)
		return 0;

	// The root inode should only use valid addresses
	if(!validAddresses(&rootInode))
		return 0;

	// The first address of the root inode shouldn't be empty
	if(rootInode.addrs[0] == 0)
		return 0;

	// Reload the root directory entry from the disk
	struct block b;
	bread(rootInode.addrs[0], &b);
	struct dirent* root = (struct dirent*)b.data;

	// If the reloaded directory entry doesn't match out init directory entry, there is a problem
	if(root[0].inum != ROOT_DIR[0].inum)
		return 0;
	
	// The '..' entry in the root directory should be equal to our ROOT_INO number of '1'
	if(root[1].inum != ROOT_INO)
		return 0;

	// Root has passed all tests.
	return 1;
}

// Returns 1 if all inodes are valid. 0 otherwise.
int inodesValidTest(){
	// Iterates through all inodes, and checks if they are valid
	int i;
	for(i = 0; i < SUPER_BLOCK->ninodes; i++){
		// If the inode isn't valid, return 0 for failure.
		if(!validInode(&INODES[i]))
			return 0;
	}

	// Success, all inodes are valid. Return 1
	return 1;
}

// Returns 1 if all addresses referenced by useable inodes are valid. Returns 0 otherwise.
int inodesAddressTest(){
	// Iterates through all inodes, and checks if they have valid addresses
	int i;
	for(i = 0; i < SUPER_BLOCK->ninodes; i++){
		// If the inode is useable, examine it further
		if(useableType(INODES[i].type)){
			// If the inode has invalid addresses, return 0
			if(!validAddresses(&INODES[i])){
				return 0;
			}
		}
	}

	// Success, so return 1
	return 1;
}

// Returns 1 if all blocks in the bitmap marked as in-use are referred to by some inode.
// If not, returns 0
int bitmapInInodesTest(){
	// Iterate through the bits in the bitmap, beginning from the data block offset, and continue
	// for the number of data blocks there are
	int i;
	for(i = DATA_OFFSET + 1; i < SUPER_BLOCK->nblocks + DATA_OFFSET; i++){
		// If the bit for this block is marked as active, examine it further
		if(blockBit(i)){
			// If this block isn't in an inode, return 0
			if(!blockInInodes(i)){
				return 0;
			}
		}
	}

	// Success, so return 1
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

// Checks if the address list of length len has only unique addresses present
int uniqueAddr(uint* addr, int len){
	int i, j;
	uint cur;

	// Iterate through the list
	for(i = 0; i < len; i++){
		// Get a value to examine
		cur = addr[i];

		// If this block is unallocated, don't worry about it
		if(cur == 0)
			continue;

		// Check that the value we're examining doesn't appear in the list again
		for(j = 0; j < len; j++){
			// If we're looking at the same entry, or this entry is unallocated,
			// don't worry about it
			if(j == i || addr[j] == 0)
				continue;

			// We've detected a duplicate address - return false
			if(cur == addr[j])
				return 0;
		}
	}

	// No duplicate addresses found - return true
	return 1;
}

// Checks if the directory inode passed to it is properly defined
int validDirect(struct dinode* inode, uint inum){
	// Get the block addresses 
	// We only need to examine the first block address
	uint* refBlocks = inode->addrs;

	// If the first address, is zero, it is unallocated, and
	// thus technically valid
	if(refBlocks[0] == 0)
		return 1;

	// Read the first address block
	struct block b;
	bread(refBlocks[0], &b);

	// Get the directory data
	struct dirent* entry = (struct dirent*)b.data;
	
	// Check the first entry refers to '.'
	if(strcmp(entry[0].name, ".") != 0)
		return 0;
	
	// Check the second entry refers to '..'
	if(strcmp(entry[1].name, "..") != 0)
		return 0;
	
	// Check that the first entry's inode refers to this inoude object
	if(entry[0].inum != inum)
		return 0;

	// All tests passed. Return true.
	return 1;

}

// Checks if the addresses of the passed inode are valid.
int validAddresses(struct dinode* inode){
	// Get the blocks pointed to by the inode
	uint* refBlocks = inode->addrs;

	// Iterate over the direct blocks
	int i;
	for(i = 0; i < NDIRECT + 1; i++){
		// If the block is unallocated, don't worry about it
		if(refBlocks[i] == 0)
			continue;
		
		// If the block address is out of range, throw an error
		if(refBlocks[i] < DATA_OFFSET || refBlocks[i] > SUPER_BLOCK->nblocks){
			printf("ERROR: bad direct address in inode.\n");
			return 0;
		}
	}

	// Check if the indirect address is utilized
	if(refBlocks[NDIRECT] != 0){
		// If so, read the indirect block
		struct block b;
		bread(refBlocks[NDIRECT], &b);

		// Get the address from the indirect block, and iterate through them
		uint* indirect = (uint*)b.data;
		for(i = 0; i < readLength(inode->size); i++){
			// If block addresses are out of range, throw an error
			if(indirect[i] < DATA_OFFSET || indirect[i] > SUPER_BLOCK->nblocks){
				printf("ERROR: bad indirect address in inode.\n");
				return 0;
			}
		}
	}

	// Otheriwse, the test has succeeded, so we return 1.
	return 1;
}

// Checks if the blockIndex given is referenced by an inode somewhere
int blockInInodes(int blockIndex){
	// Iterate through the list of inodes
	int i;
	for(i = 0; i < SUPER_BLOCK->ninodes; i++){
		// Only examine useable inodes
		if(useableType(INODES[i].type)){
			// Iterate through the direct addresses
			uint* refBlocks = INODES[i].addrs;

			int j;
			for(j = 0; j < NDIRECT + 1; j++){
				// If the blockIndex is referenced by a direct address,
				// return success
				if(refBlocks[j] == blockIndex){
					return 1;
				}
			}

			// Check if the indirect block is utilized
			if(refBlocks[NDIRECT] != 0){
				// If so, read the indirect block
				struct block b;
				bread(refBlocks[NDIRECT], &b);

				// Iterate through the indirect block
				uint* indirect = (uint*)b.data;
				for(j = 0; j < readLength(INODES[i].size); j++){
					// If the blockIndex is referenced by the indirect address,
					// return success
					if(indirect[j] == blockIndex){
						return 1;
					}
				}
			}
		}
	}

	// We have found no matches for the blockIndex given within any inode.
	// Return failure
	return 0;
}

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
		
		// Iterate through the list of blocks stored in the indirect block
		uint* indirect = (uint*)b.data;

		// Figure out how many extra blocks were needed to store the file
		for(i = 0; i < readLength(inode->size); i++){
			// Get the current block index from the indirect block
			int bIndex = indirect[i];
			
			// Check if its used by the inode
			if(bIndex == 0)
				continue;
			
			// If the current indirectly linked blocked isn't in use in the bitmap, but its
			// referenced by the inode, return false
			if(!blockInUse(bIndex))
				return 0;
		}
	}

	// The blocks referenced by the inodes and the bitmap match up - return true;
	return 1;
}

// Returns the number of reads to perform on an indirect block, based on the file size given
int readLength(int fileSize){
	// Subtract the number of used space for direct blocks from the total filesize,
	// then divide by the total block size
	int readLength = (fileSize - (NDIRECT * BLOCK_SIZE)) / BLOCK_SIZE;

	// Account for off by one errors
	readLength += ((fileSize - (NDIRECT * BLOCK_SIZE)) % BLOCK_SIZE) == 0 ? 0 : 1;
	return readLength;
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
	if(type > 0 && type < 4)
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
		fprintf(stderr, "ERROR: image not found\n");
		exit(1);
	}

	// Get info on the file system
	struct stat finfo;
	if(fstat(FSFD, &finfo) < 0){
		fprintf(stderr, "ERROR: could not load image statistics\n");
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

// Dumps a directory's data to the command line
// Indirect addressing still buggy
void debugDumpDir(struct dinode* inode){
	uint* refBlocks = inode->addrs;

	printf("----- DIR DATA -----\n");

	int i, j, directReadLength, size = inode->size;

	for(i = 0; i < NDIRECT + 1; i++){
		if(refBlocks[i] == 0)
			continue;
		
		struct block b;
		bread(refBlocks[i], &b);
		
		struct dirent* dirData = (struct dirent*)b.data;

		if(size / BLOCK_SIZE > 0){
			directReadLength = BLOCK_SIZE / sizeof(struct dirent);
			size -= BLOCK_SIZE;
		} else{
			directReadLength = size / sizeof(struct dirent);
		}
		
		for(j = 0; j < directReadLength; j++){
			if(dirData[j].inum == 0)
				continue;

			printf("%u - '%s'\n", dirData[j].inum, dirData[j].name);
		}
	}

	if(refBlocks[NDIRECT] != 0){
		struct block b;
		bread(refBlocks[NDIRECT], &b);

		uint* indirect = (uint*)b.data;

		for(i = 0; i < readLength(inode->size) / sizeof(struct dirent); i++){
			bread(indirect[i], &b);

			struct dirent* dirData = (struct dirent*)b.data;

			for(j = 0; j < BLOCK_SIZE; j++){
				if(dirData[j].inum == 0)
					continue;

				printf("%u - '%s'\n", dirData[j].inum, dirData[j].name);
			}
		}
	}
}

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

void cleanup(){
	//free(INODES);
	//free(SUPER_BLOCK);
	close(FSFD);
}
