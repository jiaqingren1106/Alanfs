#include "a1fs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdbool.h>



/**
 * Return an array of potential extent structs. since the operation uses dynamic allocation. Maker sure
 * to free the memory later.
 *
 * @param image   pointer points to the start of the image file.
 * @param sp      a1fs_superblock of the image file.
 * @param extent_count  pointer to the integer that receives the number of extent structs in the returned array.
 * 
 * @return        struct a1fs_extent *
 */
struct a1fs_extent *find_free_extents(void *image, struct a1fs_superblock *sp, int *extent_count) {

	struct a1fs_extent *free_extents = malloc(sp->datablocks_count * sizeof(struct a1fs_extent));

    unsigned char *data_bits = (unsigned char *)(image + sp->data_bitmap_pt);

	int bits = sp->datablocks_count;

	//number of extents in free_extents.
    *extent_count = 0;

    bool activate = false;
    int block_count = 0;
    int start = 0;

    for (int i = 0; i < bits; i++) {

        int index_a = i/8;
		int index_b = i%8;
		int left_shift = 7 - index_b;

        // check if bit is 0 or 1
        if ((data_bits[index_a] & (1 << left_shift)) == 0) {
            if (i == (bits-1)) {
                free_extents[*extent_count].start = start;
                free_extents[*extent_count].count = block_count+1;
                *extent_count = *extent_count + 1;
            }
            else if (activate) {
                block_count++;
            }
            else {
                activate = true;
                block_count++;
                start = i;
            }
        }

        else {
            if (activate) {
                free_extents[*extent_count].start = start;
                free_extents[*extent_count].count = block_count;
                activate = false;
                block_count = 0;
                start = 0;

                *extent_count = *extent_count + 1;

            } 
            else {continue;}
        }
    }

	// free_extents[*extent_count] = NULL;	
	return free_extents;
}



void swap(struct a1fs_extent *xp, struct a1fs_extent *yp) 
{ 
    struct a1fs_extent temp = *xp; 
    *xp = *yp; 
    *yp = temp; 
} 
void sort_extents(struct a1fs_extent *free_extents, int n) {

    int i, j; 
    for (i = 0; i < n-1; i++)       
  
       // Last i elements are already in place    
       for (j = 0; j < n-i-1; j++)  
           if (free_extents[j].count  > free_extents[j+1].count) 
              swap(&free_extents[j], &free_extents[j+1]); 
}

int sum_extents(struct a1fs_extent *free_extents, int n) {

    int sum = 0; 
    for (int i = 0; i < n; i++) {
        sum = sum + free_extents[i].count;
    }
    return sum;
}



void set_multiple_data_bitmap(void *image, struct a1fs_superblock *sp, struct a1fs_extent extent) {

    int n = extent.count;
    int data_start = (extent.start)/A1FS_BLOCK_SIZE;
    unsigned char *data_bit_start = (unsigned char *)(image + sp->data_bitmap_pt);


    for (int i = data_start; i < data_start + n; i ++) {
        int data_start_row = i/8;
        int data_start_col = i%8;
        data_bit_start[data_start_row] |= (1 << (7 - data_start_col));
    }
}


void rm_multiple_data_bitmap(void *image, struct a1fs_superblock *sp, struct a1fs_extent extent) {

    int n = extent.count;
    int data_start = (extent.start)/A1FS_BLOCK_SIZE;


    unsigned char *data_bit_start = (unsigned char *)(image + sp->data_bitmap_pt);

    for (int i = data_start; i < data_start + n; i ++) {
        int data_start_row = i/8;

        char hex;

        switch(i%8){

            case 0:
                hex = 0x7F;
                break;
            case 1:
                hex = 0xBF;
                break;
            case 2:
                hex = 0xDF;
                break;
            case 3:
                hex = 0xEF;	
                break;	
            case 4:
                hex = 0xF7;
                break;
            case 5:
                hex = 0xFB;
                break;
            case 6:
                hex = 0xFD;
                break;
            case 7:
                hex = 0xFE;
                break;
            default:
                hex = 0x00;
        }

	    data_bit_start[data_start_row] &= (hex);
    }
}


/**
 * Set the bit to 0 in inode/data bitmap.
 *
 * @param image     pointer points to the start of the image file.
 * @param sp        a1fs_superblock of the image file.
 * @param ino       the index of the inode or data block that needs to be set to 0 on bitmap.
 * @param bitmap    1 for inode bitmap pointer, 0 for data bitmap pointer.
 * 
 * @return        0 on success; -1 on error.
 */
int rm_single_bitmap(void *image, struct a1fs_superblock *sp, int ino,  int bitmap) {
    int bit;
    if (bitmap) {
        bit = sp->s_inodes_count;
    } else {
        bit = sp->datablocks_count;
    }
	if (ino >= bit) return -1;

	int index_a = ino/8;
	int index_b = ino%8;

	char hex;

	switch(index_b)
    {
        case 0:
			hex = 0x7F;
			break;
        case 1:
			hex = 0xBF;
			break;
        case 2:
			hex = 0xDF;
			break;
        case 3:
			hex = 0xEF;	
			break;	
		case 4:
			hex = 0xF7;
			break;
        case 5:
			hex = 0xFB;
			break;
        case 6:
			hex = 0xFD;
			break;
        case 7:
			hex = 0xFE;
			break;
        default:
            hex = 0x00;
    }

    unsigned char *bitmap_bits;
    if (bitmap) {
        bitmap_bits = (unsigned char *)(image + sp->inode_bitmap_pt + index_a);
    } else {
        bitmap_bits = (unsigned char *)(image + sp->data_bitmap_pt + index_a);
    }

	bitmap_bits[0] &= (hex);
	return 0;
}


/**
 * Find the first 0-bit in inode bitmap and set it to 1.
 *
 * @param image     pointer points to the start of the image file.
 * @param sp        a1fs_superblock of the image file.
 * @param result    pointer to the integer that receives the index of the bit in bitmap.
 * @param bitmap    1 for inode bitmap pointer, 0 for data bitmap pointer.

 * @return          0 on success; -1 on error.
 */
int set_single_bitmap(void *image, struct a1fs_superblock *sp, int *result, int bitmap) {
	int bit;
    unsigned char *bitmap_bits;
    if (bitmap) {
        bit = sp->s_inodes_count;
        bitmap_bits = (unsigned char *)(image + sp->inode_bitmap_pt);
    } else {
        bit = sp->datablocks_count;
        bitmap_bits = (unsigned char *)(image + sp->data_bitmap_pt);
    }


	for (int i = 0; i < bit; i++) {
		int index_a = i/8;
		int index_b = i%8;
		int left_shift = 7 - index_b;

		if ((bitmap_bits[index_a] & (1 << left_shift)) == 0) {
			 bitmap_bits[index_a] |= (1 << left_shift);
			 *result = i;
			 return 0;
		}
	}
	return -1;
}


/**
 * return the inode number of the last valid entry for the given path
 * assumption: path is not "/".
*/
int get_inode(const char *path, void *image, struct a1fs_superblock *sp, int *result){
	// Get current file or directory attributes.
	struct a1fs_inode *current_inode = (struct a1fs_inode *)(image + sp->s_first_inode);
	struct a1fs_extent *cur_extent;
	int cur_ino = 0;

	// find the inode
	char pathA[strlen(path)];
    strcpy(pathA, path+1);
    char* token = strtok(pathA, "/");
    while (token != NULL) {
		bool found = false;
		// check if current path prefix is not a directory
		if ((current_inode->mode & S_IFMT)!=S_IFDIR) {
			return -ENOTDIR;
		}

		// one extent may not hold all the entries, modify later.
		for (int j = 0; j < current_inode->extent_used; j++) {
			cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + current_inode->extend_pt + j * sizeof(a1fs_extent));

			int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
			for (int i = 0; i < entry_length; i++) {
				struct a1fs_dentry *cur_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
				if (strcmp(cur_entry->name, " ") == 0) {
					entry_length++;
					continue;	
				}
				if (strcmp(cur_entry->name, token) == 0) {
					found = true;
					// Update current file or directory attributes.
					cur_ino = cur_entry->ino;
					current_inode = (struct a1fs_inode *)(image + sp->s_first_inode + cur_ino * sizeof(a1fs_inode));
					break;
				}
			}
			if (found) {break;}
		}

		if (!found) {break;}
        token = strtok(NULL, "/");
    }

	*result = cur_ino;
	return 0;
}



/**
 * Assign the extent to inode with give size. if the size is too big for extent, return the remaining size. 
 *
 * @param block_size      size to be allocated
 * @param extent    the extent
 * @param inode     the inode
 * @param leftover  the integer that receives the remaining size.
 * 
 */
int allocate_extent(void *image, struct a1fs_superblock *sp, int block_size, struct a1fs_extent extent, struct a1fs_inode *inode, int *leftover) {

    int new_count; 
    if ((unsigned int)block_size > extent.count) {
        new_count = extent.count;
        *leftover = block_size - extent.count;
    } else {
        new_count = block_size;
        *leftover = 0;
    }
   
    if (inode->extent_used == 0) {
		struct a1fs_extent *first_extent = (image + sp->s_first_data_block + inode->extend_pt);
		first_extent->start = extent.start * A1FS_BLOCK_SIZE;
		first_extent->count = new_count;
        memset((image + sp->s_first_data_block + first_extent->start), 0, A1FS_BLOCK_SIZE*first_extent->count);

    } else {
        int extent_index = inode->extent_used;
        struct a1fs_extent *new_extent = (image + sp->s_first_data_block + inode->extend_pt + extent_index * sizeof(a1fs_extent));
        new_extent->count = new_count;
        new_extent->start = extent.start * A1FS_BLOCK_SIZE;
        memset((image + sp->s_first_data_block + new_extent->start), 0, A1FS_BLOCK_SIZE*new_extent->count);

    }
    inode->extent_used ++;
    return 0;
}



/**
 * Set the bit to 0 in inode bitmap.
 *
 * @param image   pointer points to the start of the image file.
 * @param sp      a1fs_superblock of the image file.
 * @param ino     the index of the inode that needs to be set to 0 on inode bitmap.
 * 
 * @return        0 on success; -1 on error.
 */
int rm_inode_bitmap(void *image, struct a1fs_superblock *sp, int ino) {
	int bit = sp->s_inodes_count;
	if (ino >= bit) return -1;

	int index_a = ino/8;
	int index_b = ino%8;

	char hex;

	switch(index_b)
    {
        case 0:
			hex = 0x7F;
			break;
        case 1:
			hex = 0xBF;
			break;
        case 2:
			hex = 0xDF;
			break;
        case 3:
			hex = 0xEF;	
			break;	
		case 4:
			hex = 0xF7;
			break;
        case 5:
			hex = 0xFB;
			break;
        case 6:
			hex = 0xFD;
			break;
        case 7:
			hex = 0xFE;
			break;
        default:
            hex = 0x00;
    }


	unsigned char *inode_bits = (unsigned char *)(image + sp->inode_bitmap_pt + index_a);
	inode_bits[0] &= (hex);
	return 0;
}


/**
 * Find the first 0-bit in inode bitmap and set it to 1.
 *
 * @param image   pointer points to the start of the image file.
 * @param sp      a1fs_superblock of the image file.
 * @param result  pointer to the integer that receives the index of the bit in inode bitmap.
 * 
 * @return        0 on success; -1 on error.
 */
int set_inode_bitmap(void *image, struct a1fs_superblock *sp, int *result) {
	int bit = sp->s_inodes_count;

	unsigned char *inode_bits = (unsigned char *)(image + sp->inode_bitmap_pt);

	for (int i = 0; i < bit; i++) {
		int index_a = i/8;
		int index_b = i%8;
		int left_shift = 7 - index_b;

		if ((inode_bits[index_a] & (1 << left_shift)) == 0) {
			 inode_bits[index_a] |= (1 << left_shift);
			 *result = i;
			 return 0;
		}
	}
	return -1;
}


/**
 * Find the index of the extent for the offset and the index of the offset in that extent.
 *
 * @param offset        the offset in file
 * @param image         pointer points to the start of the image file.
 * @param sp            a1fs_superblock of the image file.
 * @param inode         the inode of the file
 * @param extent_index  pointer to the integer that receives the index of extent.
 * @param byte_index    pointer to the integer that receives the index of offset in extent_index.
 */
void find_extent(void *image, struct a1fs_superblock *sp, struct a1fs_inode *inode, int offset, int *extent_index, int *byte_index) {
    int size = offset + 1;
	for (int i = 0; i < inode->extent_used; i++) {
        struct a1fs_extent *extent = (struct a1fs_extent *)(image + sp->s_first_data_block + inode->extend_pt + i * sizeof(a1fs_extent));
        size = size - (extent->count)*A1FS_BLOCK_SIZE;
        if (size > 0) {
            continue;
        }
        if (size <= 0) {
            *extent_index = i;
            *byte_index = size + (extent->count)*A1FS_BLOCK_SIZE - 1;
            break;
        }
    }
}


/**
 * Fill the file with zero starting at extent_start extent of file.
 * if EOF is at the end of the last extent, do nothing. 
 *
 * @param offset    index of starting point to fill with.
 * @param image     pointer points to the start of the image file.
 * @param sp        a1fs_superblock of the image file.
 * @param inode     the inode of the file
 */
void fill_zero(void *image, struct a1fs_superblock *sp, struct a1fs_inode *inode, int extent_start) {

    for (int i = extent_start; i < inode->extent_used; i++) {
        struct a1fs_extent *extent = (struct a1fs_extent *)(image + sp->s_first_data_block + inode->extend_pt + i * sizeof(a1fs_extent));
        memset((image + sp->s_first_data_block + extent->start), 0, extent->count * A1FS_BLOCK_SIZE);
    }

}


void dentry_sum(void *image, struct a1fs_superblock *sp, struct a1fs_extent *cur_extent, int *sum) {
    int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
    for (int i = 0; i < entry_length ; i ++) {
        struct a1fs_dentry *cur_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
        *sum += cur_entry->ino;
    }
}


void swap_extent(void *image, struct a1fs_superblock *sp, struct a1fs_extent *cur_extent, struct a1fs_inode *inode) {

    struct a1fs_extent *last_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + inode->extend_pt + (inode->extent_used-1) * sizeof(a1fs_extent));
    cur_extent->count = last_extent->count;
    cur_extent->start = last_extent->start;
}


void rm_target(void *image, struct a1fs_superblock *sp, struct a1fs_inode *inode) {
    struct a1fs_extent *extent;
    for (int i = 0; i < inode->extent_used; i++){
        extent = (struct a1fs_extent *)(image + sp->s_first_data_block + inode->extend_pt + i * sizeof(a1fs_extent));
        rm_multiple_data_bitmap(image, sp, *extent);
        int index = inode->extend_pt / A1FS_BLOCK_SIZE;
        rm_single_bitmap(image, sp, index,  0);
    }
}