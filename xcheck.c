#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>

#include "types_defs.h"
#include "fs_defs.h"

struct block {
	char* data;
};                 

void bread(int, struct block*);
void initInodes();
void init(char*);
void cleanup();

int BLOCK_SIZE = 512;
int FSFD;
char* FS_ADDR;
struct superblock* SUPER_BLOCK;
struct dinode* INODES;

int main (int argc, char *argv[]){
	if(argc < 2){
		fprintf(stderr, "Usage: xcheck <file_system_image>\n"); 
		exit(1);
	}

	init(argv[1]);

	printf("Super block: %u\n", SUPER_BLOCK->size);

	int i, j;
	for(i = 0, j=1; i < SUPER_BLOCK->ninodes; i++){
		if(INODES[i].size != 0){
			printf("Inode %d: %u\n", j, INODES[i].size);
			j++;
		}
	}

	cleanup();
	exit(0);
}

// Reads the block data at position index into a block structure
void bread(int index, struct block* b){
	b->data = malloc(sizeof(char[BLOCK_SIZE]));
	memcpy(b->data, &FS_ADDR[index * BLOCK_SIZE], BLOCK_SIZE);
}

// Inits the list of inodes for the file system
// Called after the super block is instantiated
void initInodes(){
	INODES = malloc(sizeof(struct dinode[SUPER_BLOCK->ninodes]));
	int i, j, bI = 2;

	struct block* buf = malloc(sizeof(*buf));
	bread(bI, buf);
	for(i = 0, j = 0; i < SUPER_BLOCK->ninodes; i++, j++){
		if((sizeof(struct dinode) * j) >= BLOCK_SIZE){
			j = 0;
			free(buf->data);
			bread(++bI, buf);
		}

		memcpy(&INODES[i], &buf->data + (j * sizeof(struct dinode)), sizeof(struct dinode));
	}

	free(buf->data);
	free(buf);
}

// Init prerequisite data and structures before
// filesystem analysis begins
void init(char* fileName){
	FSFD = open(fileName, O_RDONLY);

	if(FSFD < 0){
		fprintf(stderr, "Error opening file!\n");
		exit(1);
	}

	FS_ADDR = mmap(0, 512, PROT_READ, MAP_PRIVATE, FSFD, 0);

	struct block* buf = malloc(sizeof(*buf));
	bread(1, buf);
	SUPER_BLOCK = malloc(sizeof(*SUPER_BLOCK));
	memcpy(SUPER_BLOCK, buf->data, sizeof(struct superblock));

	free(buf->data);
	free(buf);

	initInodes();
}

// Cleanup the application
void cleanup(){
	free(INODES);
	free(SUPER_BLOCK);
	close(FSFD);
}
