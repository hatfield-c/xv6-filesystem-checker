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
void init(char*);
void cleanup();

int BLOCK_SIZE = 512;
int FSFD;
char* FS_ADDR;
struct superblock SUPER_BLOCK;

int main (int argc, char *argv[]){
	if(argc < 2){
		fprintf(stderr, "Usage: xcheck <file_system_image>\n"); 
		exit(1);
	}

	init(argv[1]);

	/*FSFD = open(argv[1], O_RDONLY);
                                                         
	if(FSFD < 0){
		fprintf(stderr, "Error opening file!\n");
		exit(1);
	}
                                                         
	FS_ADDR = mmap(0, 512, PROT_READ, MAP_PRIVATE, FSFD, 0);
	
	struct superblock* sbp = &SUPER_BLOCK;

	struct block b;
	bread(1, &b);
	//char data[BLOCK_SIZE];

	//memcpy(data, &FS_ADDR[512], BLOCK_SIZE);
	//b.data = data;

	memcpy(sbp, b.data, sizeof(struct superblock));*/

	//uint i = SUPER_BLOCK.size;
	printf("Super block: %u\n", SUPER_BLOCK.size);

	cleanup();
	exit(0);
}

void bread(int index, struct block* b){
	char data[BLOCK_SIZE];
	memcpy(data, &FS_ADDR[index * BLOCK_SIZE], BLOCK_SIZE);
	b->data = data;
}

void init(char* fileName){
	FSFD = open(fileName, O_RDONLY);

	if(FSFD < 0){
		fprintf(stderr, "Error opening file!\n");
		exit(1);
	}

	FS_ADDR = mmap(0, 512, PROT_READ, MAP_PRIVATE, FSFD, 0);

	struct block buf;
	bread(1, &buf);
	struct superblock* sbp = &SUPER_BLOCK;
	memcpy(sbp, buf.data, sizeof(struct superblock));
}

void cleanup(){
	close(FSFD);
}
