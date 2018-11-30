#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/stat.h>

#include "types_defs.h"
#include "fs_defs.h"

#define BLOCK_SIZE 512
#define ROOT_INO 1
#define INODE_PB (BLOCK_SIZE / sizeof(struct dinode))

struct block {
	char* data;
};                 

void bread(int, struct block*);
void init(char*);
int inode2Block(int);
void debugDumpBlock(struct block, int);
void cleanup();

int FSFD;
char* FS_ADDR;
struct superblock* SUPER_BLOCK;
struct dinode* INODES;
struct dirent* ROOT_DIR;
char* BMAP;

int main (int argc, char *argv[]){
	if(argc < 2){
		fprintf(stderr, "Usage: xcheck <file_system_image>\n"); 
		exit(1);
	}

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

	uint* addr = INODES[1].addrs;
	printf("[%u]\n", addr[0]);

	cleanup();
	exit(0);
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

	struct stat finfo;
	if(fstat(FSFD, &finfo) < 0){
		fprintf(stderr, "Error loading file system info!\n");
		exit(1);
	}

	int fsize = finfo.st_size;
	FS_ADDR = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, FSFD, 0);
	if(FS_ADDR == MAP_FAILED){
		fprintf(stderr, "Error mapping file system into memory!");
	}

	struct block b;
	bread(1, &b);
	
	SUPER_BLOCK = (struct superblock*) b.data;

	bread(2, &b);

	INODES = (struct dinode*)b.data;

	bread(INODES[ROOT_INO].addrs[0], &b);
	ROOT_DIR = (struct dirent*)b.data;

	// Read bitmap
	int bmBlock = inode2Block(SUPER_BLOCK->ninodes) + 1;
	bread(bmBlock, &b);
	BMAP = b.data;
}

int inode2Block(int inodeIndex){
	return (inodeIndex / INODE_PB) + 2;
}

void debugDumpBlock(struct block b, int MAX){
	int i;
	for(i = 0; i < MAX; i++){
		printf("[%u]\n", b.data[i]);
	}
}

// Cleanup the application
void cleanup(){
	//free(INODES);
	//free(SUPER_BLOCK);
	close(FSFD);
}
