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
int inodesInBitmapTest();

// Basic utility prototypes
int inodeInBitmap(struct dinode*);
int useableType(int);
int validInode(struct dinode*);
int blockBit(int);
void bread(int, struct block*);
void init(char*);
int inode2Block(int);
void debugPrintByte(char);
void debugDumpBlock(struct block, int);
void int2Binary(int, char[8]);
void cleanup();

// File descriptor of file system
int FSFD;

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

/*	int i;
	for(i = 0; i < INODES[1].size / sizeof(struct dirent); i++){
		struct dirent dir = ROOT_DIR[i];
		printf("[%d] [%s]\n", dir.inum, dir.name);
	}
*/
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
	if(inodesInBitmapTest()){
		printf("Success!\n");
	} else {
		printf("Error!\n");
	}

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

// Returns 1 if for all in-use inodes, each block in use is also marked in-use by the
// bitmap. Returns 0 if an inode is using a block which is not marked as in-use by the
// bitmap.
int inodesInBitmapTest(){
	int i;
	for(i = 0; i < SUPER_BLOCK->ninodes; i++){
		if(useableType(INODES[i].type)){
			if(!inodeInBitmap(&INODES[i]))
				return 0;
		}
	}
	
	return 1;
}

// ***
// *
// *   Utility functions
// *
// ***

int inodeInBitmap(struct dinode* inode){
	uint* refBlocks = inode->addrs;

	int i;
	for(i = 0; i < NDIRECT + 1; i++){
		if(refBlocks[i] == 0)
			continue;

		if(refBlocks[i] < 2)
			return 0;

		if(refBlocks[i] > SUPER_BLOCK->nblocks - 1)
			return 0;
		
		if(!blockBit(refBlocks[i]))
			return 0;
	}

	return 1;
}

int useableType(int type){
	int i;
	for(i = 1; i < 4; i++){
		if(type == i)
			return 1;
	}

	return 0;
}

// Checks if the inode given to it is valid
int validInode(struct dinode* inode){
	if(!useableType(inode->type)){
		if(inode->type == T_UNALLOC)
			return 1;

		return 0;
	}
	
	return 1;
}

// Gets the bit from the bitmap at the given index
int blockBit(int index){
	if(index < 1 || index > SUPER_BLOCK->nblocks)
		return 0;

	int bit;
	int byte = index / 8;
	int bitPos = index % 8;
	
	char raw = BMAP[byte];
	raw = raw >> bitPos;
	raw = raw & 0x1;

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
}

// Returns the block which an inode at inodeIndex is located in
int inode2Block(int inodeIndex){
	return (inodeIndex / INODE_PB) + 2;
}

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
