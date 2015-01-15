
#include "tools.h"

/* in result, file pointer is pointing to the begin of i-node bitmap */
int copy_file_to_vd(char *filename, char *vd_name){
	FILE *file_to_copy;
	FILE *disk;
	FILE *secondDescriptor; /* helper descriptor */
	char *inode_bitmap;
	char *data_bitmap;
	char buffer[BLOCK_SIZE];
	struct stat st;
	double size_file_to_copy;
	superblock block;
	unsigned char mask;
	unsigned char byte; /*variable for checking bitmaps  and holding temporary bytes*/
	int temp;
	int counter;
	inode temp_inode;
	file_to_copy = fopen(filename, "rb");
	if (file_to_copy == NULL){
		perror("Cannot open file to copy disk");
		return -1;
	}
	stat(filename, &st);
	size_file_to_copy = st.st_size;
	disk = fopen(vd_name, "r+b");
	if (disk == NULL){
		perror("Cannot open virtual disk");
		return -1;
	}
	read_and_check_superblock(&block, disk);
	int blocks_needed_to_copy_file = ceil(size_file_to_copy / BLOCK_SIZE);
	
	if (block.free_block_number < blocks_needed_to_copy_file){
		printf("Too few space on virtual disk!");
		return -1;
	}
	inode_bitmap = (char *)malloc(block.bytes_for_bitmap);
	data_bitmap = (char *)malloc(block.bytes_for_bitmap);
	read_bitmap_blocks(block.bytes_for_bitmap, inode_bitmap, data_bitmap, disk);
	/* First, we have to check if we have file, which name is the same as file to copy*/
	/* TODO: Add check to &IN USE */
	secondDescriptor = fopen(vd_name, "r+b");
	/* move second descriptor  to the inode bitmap*/
	kseek(disk, BLOCK_SIZE, SEEK_SET);
	mask = 0x80;
	counter = 0;
	for (int i = 0; i < block.inode_number; i++){
		byte = inode_bitmap[counter];
		byte = mask & byte;
		kread(&temp_inode, sizeof(inode), disk);
		if (strcmp(filename, temp_inode.filename) == 0 && byte > 0){ /* We have file like that on disk - */
			printf("jest taki sam");
			/*we have to count, have many blocks this file consisted */
			int blocks_for_old_file = ceil((double)temp_inode.size_of_file / BLOCK_SIZE);
			if (blocks_needed_to_copy_file <= blocks_for_old_file + block.free_block_number){
				remove_file_from_vd(filename, vd_name); /* Delete it */
					/*File is removed, it's time to write new file*/
				/*But first we have to re-check superblock - remove file could change it! */
				kseek(disk, 0, SEEK_SET);
				read_and_check_superblock(&block, disk);
				break;
			}
			else{
				printf("New file is too big!");
				return -1;
			}
		}
		if (fseek(disk, block.inode_size - sizeof(inode), SEEK_CUR) != 0){
			perror("Cannot move file pointer");
		}
		if (mask == 0){
			mask = 0x80;
			counter ++;
		}
	}
	fclose(secondDescriptor);
	if (block.free_inode_number == 0){
		printf("Too many files on virtual disk\n");
	}
	kseek(disk, 3 * BLOCK_SIZE, SEEK_SET);
	/* File pointer is at the beginning of inode structure table */
	mask = 0x80;
	short *pointers_to_blocks = (short *)malloc(sizeof(short) * block.block_number);
	temp = 0; /* temporary variable, used to properly moving file pointer across inode table */
	counter = 0;
	for (int i = 0; i < block.inode_number; i++){
		byte = inode_bitmap[counter];
		unsigned char result = mask & byte;
		if (result == 0){
			/* We have found unused i-node */
			/* Firstly move pointer to this inode in inode-table */
			if (fseek(disk, i * block.inode_size, SEEK_CUR) != 0){
				perror("Cannot move file pointer");
			}
			inode i_node;
			write_string_to_array(filename, i_node.filename);
			i_node.size_of_file = size_file_to_copy;
			inode_bitmap[counter] = inode_bitmap[counter] | mask;
			block.free_inode_number--;
			/*Now write i-node description to i_node table */
			printf("przed zapisaniem inode description: %lu\n", ftell(disk));
			kwrite(&i_node, sizeof(inode), disk);
			/* Inode description is written - now if file is non 0 size - take care of data blocks */
			/* Now read pointers, update them, and write to the */
			kread(pointers_to_blocks, sizeof(short) * block.block_number, disk);
			if (size_file_to_copy > 0){ 
				mask = 0x80;
				counter = 0;
				int pointer_counter = 0;
				secondDescriptor = fopen(vd_name, "r+b"); 
				temp = ceil((double)block.inode_size * block.block_number / BLOCK_SIZE); /*Block number for inode_structure_table */
				kseek(secondDescriptor, (3 + temp) * BLOCK_SIZE, SEEK_CUR);
				for(int i = 0; i < block.block_number; i++){
					byte = data_bitmap[counter];
					result = mask & byte;
					if (result == 0){/*We have found data block  - bit is 0!*/
						temp = size_file_to_copy > BLOCK_SIZE ? BLOCK_SIZE : size_file_to_copy;
						size_file_to_copy -= BLOCK_SIZE;
						kread(buffer, temp, file_to_copy);
						kwrite(buffer, temp, secondDescriptor);
						block.free_block_number --;
						kseek(secondDescriptor, BLOCK_SIZE, SEEK_CUR);
						pointers_to_blocks[pointer_counter] = i;
						data_bitmap[counter] = data_bitmap[counter] | mask;
						mask = mask >> 1;
						if (mask == 0){
							mask = 0x80;
							counter ++;
						}
						if (size_file_to_copy < 0){	
							fclose(secondDescriptor);
							kseek(disk, -sizeof(short) * block.block_number, SEEK_CUR);
							kwrite(pointers_to_blocks, sizeof(short) * block.block_number, disk);
							break;							
						}	
					}/* do result */
				}/* do for */
			}/* fo file size */
			/* I-node is now used by the file system - wrote information about it into the bitmap file */
			break;
		}/* result */

		mask = mask >> 1;
		if (mask == 0){
			mask = 0x80;
			counter ++;
		}
	}/* koniec fora*/
	kseek(disk, 0, SEEK_SET);
	kwrite(&block, sizeof(superblock), disk);
	kseek(disk, BLOCK_SIZE - sizeof(superblock), SEEK_CUR);
	kwrite(inode_bitmap, block.bytes_for_bitmap, disk);
	kseek(disk, 2 * BLOCK_SIZE, SEEK_SET);
	kwrite(data_bitmap, block.bytes_for_bitmap, disk);
	//Debug

	
	free(inode_bitmap);
	free(pointers_to_blocks);
	free(data_bitmap);
	fclose(file_to_copy);
	fclose(disk);
	return 0;

}
int main(int argc, char **argv){
	
	copy_file_to_vd(argv[1], argv[2]);
	
	return 0;
}