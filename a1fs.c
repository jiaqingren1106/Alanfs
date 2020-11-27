/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2020 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "helper.c"
#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"
#include <libgen.h>
//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help
	if (opts->help) return true;

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) return false;

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));
	st->f_bsize   = A1FS_BLOCK_SIZE;
	st->f_frsize  = A1FS_BLOCK_SIZE;
	//TODO: fill in the rest of required fields based on the information stored
	// in the superblock
	// get attributes from the struct fs
	void *image = fs->image;
	size_t size = fs->size;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	// set fields in statvfs *st
	int blocks_num = 0;
	if (size % A1FS_BLOCK_SIZE) {
		blocks_num = size/A1FS_BLOCK_SIZE + 1;
	} else {
		blocks_num = size/A1FS_BLOCK_SIZE;
	}
	st->f_blocks  = blocks_num;
	st->f_bfree   = blocks_num - sp->blocks_usd;
	st->f_bavail  = blocks_num - sp->blocks_usd;
	st->f_files   = sp->s_inodes_count;
	st->f_ffree   = sp->s_inodes_count - sp->inodes_usd;
	st->f_favail  = sp->s_inodes_count - sp->inodes_usd;
	st->f_namemax = A1FS_NAME_MAX;

	
	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors).
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{	
	if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));

	//TODO: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode

	// attribute from fs_ctx *fs
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	//check if path is valid
	if ((path[0] != '/') && (path[0] != '.')){
		return -ENOENT;
	} 

	//check if path is root
	if (strcmp(path, "/") == 0){
		struct a1fs_inode *root = (struct a1fs_inode *)(image + sp->s_first_inode);

		st->st_mode = S_IFDIR | 0777;
		st->st_nlink = root->links;
		st->st_size = root->size;
		st->st_blocks = root->size / 512;
		st->st_mtim = root->mtime;
		return 0;
	} 
	
	// Get current file or directory attributes starting from root.
	struct a1fs_inode *current_inode = (struct a1fs_inode *)(image + sp->s_first_inode);
	struct a1fs_extent *cur_extent;
	int cur_ino = 0;

	// traverse the path and verify that path is valid.
	char pathA[strlen(path)];
    strcpy(pathA, path+1);
    char* token = strtok(pathA, "/");
    while (token != NULL) {
		bool found = false;
		// check if current path prefix is not a directory
		if ((current_inode->mode & S_IFMT)!=S_IFDIR) {
			return -ENOTDIR;
		}

		// traverse the extents in the inodes. In each extent, travese the dentry to find the component.
		for (int j = 0; j < current_inode->extent_used; j++) {
			cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + current_inode->extend_pt + j * sizeof(a1fs_extent));

			int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
			for (int i = 0; i < entry_length; i++) {
				struct a1fs_dentry *cur_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
				
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

		if (!found){
			token = strtok(NULL, "/");
			return (token == NULL)? -ENOENT : -ENOTDIR;
		}
        token = strtok(NULL, "/");
    }
	
	st->st_mode   = current_inode->mode;
	st->st_nlink  = current_inode->links;
	st->st_size   = current_inode->size;
	st->st_blocks = current_inode->size / 512;
	st->st_mtim   = current_inode->mtime;

	return 0;
}


/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: lookup the directory inode for given path and iterate through its
	// directory entries
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);
	
	//check if path is valid
	if ((path[0] != '/')){
		return -ENOENT;
	} 
	//check if path is root, if it is, write each entry into filler.
	if (strcmp(path, "/") == 0){
		struct a1fs_extent *cur_extent;
		struct a1fs_inode *root = (struct a1fs_inode *)(image + sp->s_first_inode);
		int entry_check = root->size/sizeof(a1fs_dentry);

		for (int j = 0; j < root->extent_used; j++) {

			if (entry_check == 0) {break;}
			cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + root->extend_pt + j * sizeof(a1fs_extent));

			int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
			for (int i = 0; i < entry_length; i++) {
				if (entry_check == 0) {break;}
				struct a1fs_dentry *root_dir_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
				if (strcmp(root_dir_entry->name, " ") == 0) {
					continue;
				}
				int i = filler(buf, root_dir_entry->name, NULL, 0);
				if (i != 0){
					return -ENOMEM;
				}
				entry_check -= 1;
			}
		}
		return 0;
	} 

	// Get current file or directory attributes.
	struct a1fs_inode *current_inode = (struct a1fs_inode *)(image + sp->s_first_inode);
	struct a1fs_extent *cur_extent;
	int cur_ino = 0;

	// traverse the path and get the inode of the last component.
	char pathA[strlen(path)];
    strcpy(pathA, path+1);
    char* token = strtok(pathA, "/");
    while (token != NULL) {
		bool found = false;
		// check if current path prefix is not a directory
		if ((current_inode->mode & S_IFMT)!=S_IFDIR) {
			return -ENOTDIR;
		}

		// traverse the extents in the inodes. In each extent, travese the dentry to find the component.
		for (int j = 0; j < current_inode->extent_used; j++) {
			cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + current_inode->extend_pt + j * sizeof(a1fs_extent));

			int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
			for (int i = 0; i < entry_length; i++) {
				struct a1fs_dentry *cur_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
				
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

		if (!found){
			token = strtok(NULL, "/");
			return (token != NULL)? -ENOENT : 0;
		}
        token = strtok(NULL, "/");
    }

	// call filler on each entry.
	int entry_check = current_inode->size/sizeof(a1fs_dentry);

	for (int j = 0; j < current_inode->extent_used; j++) {
		if (entry_check == 0) {break;}
		cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + current_inode->extend_pt + j * sizeof(a1fs_extent));
		
		int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
		for (int i = 0; i < entry_length; i++) {
			if (entry_check == 0) {break;}
			struct a1fs_dentry *cur_dentry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
			if (strcmp(cur_dentry->name, " ") == 0) {
				continue;
			}
			int i = filler(buf, cur_dentry->name, NULL, 0);
			if (i != 0){
				return -ENOMEM;
			}
			entry_check -= 1;
		}
	}
	return 0;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();

	//TODO: create a directory at given path with given mode

	// attribute from fs_ctx *fs
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	// get the parent inode number.
	int parent_inode;
	get_inode(path, image, sp, &parent_inode);

	/** if parent does not have extent, initalize a extent block and a extent.
	*/
	struct a1fs_inode *parent = (struct a1fs_inode *)(image + sp->s_first_inode + parent_inode * sizeof(a1fs_inode));
	
	if (parent->extent_used == 0){
		int extent_pt_index;
		if (set_single_bitmap(image, sp, &extent_pt_index, 0) == -1) {
			return -ENOSPC;
		}
		parent->extend_pt = extent_pt_index * A1FS_BLOCK_SIZE;

		int first_extent_pt;
		if (set_single_bitmap(image, sp, &first_extent_pt, 0) == -1) {
			return -ENOSPC;
		}
		struct a1fs_extent *first_extent = (image + sp->s_first_data_block + parent->extend_pt);
		first_extent->start = first_extent_pt * A1FS_BLOCK_SIZE;
		first_extent->count = 1;
		memset((image + sp->s_first_data_block + first_extent->start), 0, A1FS_BLOCK_SIZE*first_extent->count);
		parent->extent_used ++;
	}

	parent->links += 1;

	/** find avaliable space in inode bitmap and update inode bitmap. */
	int new_ino;
	int error = set_inode_bitmap(image, sp, &new_ino);
	//check if there is avaliable inode in the file system.
	if (error < 0) return -ENOSPC;

	/** add new a1fs_dentry to parent directory block and set its attribute. */
	int entry_stored = parent->size / sizeof(a1fs_dentry);
	struct a1fs_extent *cur_extent;
	bool found = false;

	for (int j = 0; j < parent->extent_used; j++) {
		cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + parent->extend_pt + j * sizeof(a1fs_extent));

		int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
		for (int i = 0; i < entry_length; i++) {
			struct a1fs_dentry *cur_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
			if ((strcmp(cur_entry->name, " ") == 0) || (entry_stored == 0)) {
				found = true;

				char pathA[PATH_MAX]; 
				strcpy(pathA, path);
				char *name = basename(pathA);
				strcpy(cur_entry->name, name);
				cur_entry->ino = new_ino;
				break;	
			} 
			entry_stored --;
		}
		if (found) {break;}
	}

    /**find new extent and add it to the inode*/
	if (found == false) {
		int extent_bk;
		if (set_single_bitmap(image, sp, &extent_bk, 0) == -1) {
			return -ENOSPC;
		}
		struct a1fs_extent *new_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + parent->extend_pt + parent->extent_used * sizeof(a1fs_extent));
		new_extent->start = extent_bk * A1FS_BLOCK_SIZE;
		new_extent->count = 1;
		memset((image + sp->s_first_data_block + new_extent->start), 0, A1FS_BLOCK_SIZE*new_extent->count);
		parent->extent_used ++;

		struct a1fs_dentry *new_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + new_extent->start);
		char pathA[PATH_MAX]; 
		strcpy(pathA, path);
		char *name = basename(pathA);
		strcpy(new_entry->name, name);
		new_entry->ino = new_ino;
	}

	/** create inode and set inode attribute */
	struct a1fs_inode *new_inode = (struct a1fs_inode *)(image + sp->s_first_inode + new_ino*sizeof(a1fs_inode));
	new_inode->mode  = mode;
	new_inode->links = 2;
	new_inode->size  = 0;
	int time_updated_or_not = clock_gettime(CLOCK_REALTIME, &new_inode->mtime);
	if (time_updated_or_not == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

	/** update parent inode attribute */
	parent->size += sizeof(a1fs_dentry);
	parent->mtime = new_inode->mtime;

	/** update super block*/
	sp->inodes_usd += 1;

	return 0;
}


/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();

	//TODO: remove the directory at given path (only if it's empty)
	// attribute from fs_ctx *fs
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	// get the inode number of the target and its parent directory
	char pathA[PATH_MAX]; 
	strcpy(pathA, path);
	char *path_dir = dirname(pathA);
	int parent_inode;
	get_inode(path_dir, image, sp, &parent_inode);
	int target_inode;
	get_inode(path, image, sp, &target_inode);

	/** set inode bitmap to 0 for target inode */
	struct a1fs_inode *target_dir = (struct a1fs_inode *)(image + sp->s_first_inode + target_inode * sizeof(a1fs_inode));
	if (target_dir->size != 0) {
		return -ENOTEMPTY;
	}
	rm_inode_bitmap(image, sp, target_inode);

	/** find target a1fs_dentry in parent entry list and set ino to -1. Update parent inode attributes */
	struct a1fs_inode *parent = (struct a1fs_inode *)(image + sp->s_first_inode + parent_inode * sizeof(a1fs_inode));
	parent->links -= 1;
	parent->size -= sizeof(a1fs_dentry);
	int time_updated_or_not = clock_gettime(CLOCK_REALTIME, &parent->mtime);
	if (time_updated_or_not == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }


	struct a1fs_extent *cur_extent;
	for (int j = 0; j < parent->extent_used; j++) {
		cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + parent->extend_pt + j * sizeof(a1fs_extent));

		int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
		for (int i = 0; i < entry_length; i++) {
			struct a1fs_dentry *cur_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
			
			if (cur_entry->ino == (unsigned int) target_inode) {
				strcpy(cur_entry->name, " ");
				(cur_entry->name)[1] = '\0';
				cur_entry->ino = 0;
				break;
			}
		}

		// check extent size. if size == 0, delete the extent
		int sum = 0;
		dentry_sum(image, sp, cur_extent, &sum);
		if (sum == 0) {
			int rm_index = cur_extent->start/A1FS_BLOCK_SIZE;
			rm_single_bitmap(image, sp, rm_index,  0);
			swap_extent(image, sp, cur_extent, parent);
			parent->extent_used -- ;
		}
	}

	//check size of parent directory. and update data bitmap is necessary.
	if (parent->extent_used == 0) {
		// free extent block pointer
		int index = parent->extend_pt / A1FS_BLOCK_SIZE;
		rm_single_bitmap(image, sp, index,  0);
	}


	/** update super blcok*/
	sp->inodes_usd -= 1;


	return 0;
}


/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	//TODO: create a file at given path with given mode
	// attribute from fs_ctx *fs
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	int parent_inode;
	get_inode(path, image, sp, &parent_inode);

	/** currently assuming directory is small, which only uses first extent and does not 
	 * use all the first extent of the inode. modify later
	*/
	struct a1fs_inode *parent = (struct a1fs_inode *)(image + sp->s_first_inode + parent_inode * sizeof(a1fs_inode));
	
    /** assume only use one extent */
	if (parent->extent_used == 0){
		int extent_pt_index;
		if (set_single_bitmap(image, sp, &extent_pt_index, 0) == -1) {
			return -ENOSPC;
		}
		parent->extend_pt = extent_pt_index * A1FS_BLOCK_SIZE;

		int first_extent_pt;
		if (set_single_bitmap(image, sp, &first_extent_pt, 0) == -1) {
			return -ENOSPC;
		}
		struct a1fs_extent *first_extent = (image + sp->s_first_data_block + parent->extend_pt);
		first_extent->start = first_extent_pt * A1FS_BLOCK_SIZE;
		first_extent->count = 1;
		memset((image + sp->s_first_data_block + first_extent->start), 0, A1FS_BLOCK_SIZE*first_extent->count);
		parent->extent_used ++;
	}


	/** find avaliable space in inode bitmap and update inode bitmap. */
	int new_ino;
	int error = set_inode_bitmap(image, sp, &new_ino);
	//check if there is avaliable inode in the file system.
	if (error < 0) return -ENOSPC;

	/** add new a1fs_dentry to parent directory block and set its attribute. */
	int entry_stored = parent->size / sizeof(a1fs_dentry);
	struct a1fs_extent *cur_extent;
	bool found = false;

	for (int j = 0; j < parent->extent_used; j++) {
		cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + parent->extend_pt + j * sizeof(a1fs_extent));

		int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
		for (int i = 0; i < entry_length; i++) {
			struct a1fs_dentry *cur_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
			if ((strcmp(cur_entry->name, " ") == 0) || (entry_stored == 0)) {
				found = true;

				char pathA[PATH_MAX]; 
				strcpy(pathA, path);
				char *name = basename(pathA);
				strcpy(cur_entry->name, name);
				cur_entry->ino = new_ino;
				break;	
			} 
			entry_stored --;
		}
		if (found) {break;}
	}


	if (found == false) {
		int extent_bk;
		if (set_single_bitmap(image, sp, &extent_bk, 0) == -1) {
			return -ENOSPC;
		}
		struct a1fs_extent *new_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + parent->extend_pt + parent->extent_used * sizeof(a1fs_extent));
		new_extent->start = extent_bk * A1FS_BLOCK_SIZE;
		new_extent->count = 1;
		memset((image + sp->s_first_data_block + new_extent->start), 0, A1FS_BLOCK_SIZE*new_extent->count);
		parent->extent_used ++;

		struct a1fs_dentry *new_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + new_extent->start);
		char pathA[PATH_MAX]; 
		strcpy(pathA, path);
		char *name = basename(pathA);
		strcpy(new_entry->name, name);
		new_entry->ino = new_ino;
	}

	/** create inode and set inode attribute */
	struct a1fs_inode *new_inode = (struct a1fs_inode *)(image + sp->s_first_inode + new_ino*sizeof(a1fs_inode));
	new_inode->mode  = mode;
	new_inode->links = 1;
	new_inode->size  = 0;
	int time_updated_or_not = clock_gettime(CLOCK_REALTIME, &new_inode->mtime);
	if (time_updated_or_not == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

	/** update parent inode attribute */
	parent->size += sizeof(a1fs_dentry);
	parent->mtime = new_inode->mtime;

	/** update super block*/
	sp->inodes_usd += 1;

	return 0;
}


/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	//TODO: remove the file at given path
	
	/** 
	 * assume the file is empty. modify later.
	*/
	// attribute from fs_ctx *fs
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	// get the inode number of the target and its parent directory
	char pathA[PATH_MAX]; 
	strcpy(pathA, path);
	char *path_dir = dirname(pathA);
	int parent_inode_index;
	get_inode(path_dir, image, sp, &parent_inode_index);
	int target_inode_index;
	get_inode(path, image, sp, &target_inode_index);

	/** set inode bitmap and data bitmap to 0 for target inode */
	struct a1fs_inode *target_inode = (struct a1fs_inode *)(image + sp->s_first_inode + target_inode_index * sizeof(a1fs_inode));
	rm_inode_bitmap(image, sp, target_inode_index);
	rm_target(image, sp, target_inode);

	/** find target a1fs_dentry in parent entry list and set ino to 0. Update parent inode attributes */
	struct a1fs_inode *parent = (struct a1fs_inode *)(image + sp->s_first_inode + parent_inode_index * sizeof(a1fs_inode));
	parent->links -= 1;
	parent->size -= sizeof(a1fs_dentry);
	int time_updated_or_not = clock_gettime(CLOCK_REALTIME, &parent->mtime);
	if (time_updated_or_not == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }



	struct a1fs_extent *cur_extent;
	for (int j = 0; j < parent->extent_used; j++) {
		cur_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + parent->extend_pt + j * sizeof(a1fs_extent));

		int entry_length = (cur_extent->count) * A1FS_BLOCK_SIZE / sizeof(a1fs_dentry);
		for (int i = 0; i < entry_length; i++) {
			struct a1fs_dentry *cur_entry = (struct a1fs_dentry *)(image + sp->s_first_data_block + cur_extent->start + i * sizeof(a1fs_dentry));
			
			if (cur_entry->ino == (unsigned int)target_inode_index) {
				strcpy(cur_entry->name, " ");
				(cur_entry->name)[1] = '\0';
				cur_entry->ino = 0;
				break;
			}
		}
		// check extent size. if size == 0, delete the extent
		int sum = 0;
		dentry_sum(image, sp, cur_extent, &sum);
		if (sum == 0) {
			int rm_index = cur_extent->start/A1FS_BLOCK_SIZE;
			rm_single_bitmap(image, sp, rm_index,  0);
			swap_extent(image, sp, cur_extent, parent);
			parent->extent_used -- ;
		}
	}

	
	//check size of parent directory. and update data bitmap is necessary.
	if (parent->extent_used == 0) {
		// free extent block pointer
		int index = parent->extend_pt / A1FS_BLOCK_SIZE;
		rm_single_bitmap(image, sp, index,  0);
	}

	/** update super blcok*/
	sp->inodes_usd -= 1;

	return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();

	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);
	// find the inode number that needs to be updated time
	int target_inode_index;
	get_inode(path, image, sp, &target_inode_index);

	char pathA[PATH_MAX]; 
	strcpy(pathA, path);
	char *path_dir = dirname(pathA);
	
	int parent_inode_index;
	get_inode(path_dir, image, sp, &parent_inode_index);

	struct a1fs_inode *target_inode = (struct a1fs_inode *)(image + sp->s_first_inode + target_inode_index * sizeof(a1fs_inode));
	struct a1fs_inode *parent_inode = (struct a1fs_inode *)(image + sp->s_first_inode + parent_inode_index * sizeof(a1fs_inode));
	// check if the time arguement has content
	if (times != NULL){
		target_inode->mtime = times[1];
		parent_inode->mtime = times[1];
		return 0;
	}
	// else update the inode with current time
	int time_updated_or_not = clock_gettime(CLOCK_REALTIME, &target_inode->mtime);
	int time_updated_or_not_2 = clock_gettime(CLOCK_REALTIME, &parent_inode->mtime);
	if (time_updated_or_not == -1 || time_updated_or_not_2 == -1) {
        perror("clock_gettime");
		return -ENOSYS;
	}
	return 0;
}


/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	//TODO: set new file size, possibly "zeroing out" the uninitialized range
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	int target_inode_index;
	get_inode(path, image, sp, &target_inode_index);
	struct a1fs_inode *target_inode = (struct a1fs_inode *)(image + sp->s_first_inode + target_inode_index * sizeof(a1fs_inode));

	if (size == 0) {
		return a1fs_unlink(path);
	}

	int target_blocks = (((target_inode->size)%A1FS_BLOCK_SIZE)==0) ? target_inode->size/A1FS_BLOCK_SIZE : target_inode->size/A1FS_BLOCK_SIZE + 1;
	int size_blocks = ((size%A1FS_BLOCK_SIZE)==0) ? size/A1FS_BLOCK_SIZE : size/A1FS_BLOCK_SIZE + 1;
	int old_size = target_inode->size;


	if (target_blocks == size_blocks) {
		return 0;
	}

	if (target_blocks < size_blocks) {

		if (target_inode->extent_used == 0){
			int extent_pt_index;
			if (set_single_bitmap(image, sp, &extent_pt_index, 0) == -1) {
				return -ENOSPC;
			}
			target_inode->extend_pt = extent_pt_index * A1FS_BLOCK_SIZE;
		}

		int blocks_required = size_blocks - target_blocks;

		int extent_count;
		struct a1fs_extent *free_extents = find_free_extents(image, sp, &extent_count);
		if (sum_extents(free_extents, extent_count) < blocks_required) {return -ENOSPC;}
		sort_extents(free_extents, extent_count);

		int extent_index = 0;

		while (blocks_required != 0) {
			allocate_extent(image, sp, blocks_required, free_extents[extent_index], target_inode, &blocks_required);
			struct a1fs_extent *last_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + target_inode->extend_pt + ((target_inode->extent_used)-1) * sizeof(a1fs_extent));
			set_multiple_data_bitmap(image, sp, *last_extent);
			extent_index += 1;
		}
		free(free_extents);
		target_inode->size = size;


		int ext_index;
		int byte_index;
		find_extent(image, sp, target_inode, old_size-1, &ext_index, &byte_index);
		struct a1fs_extent *target_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + target_inode->extend_pt + ext_index*sizeof(a1fs_extent));
		int residue = target_extent->count * A1FS_BLOCK_SIZE - (byte_index+1);

		if (residue > 0) {
			memset((image + sp->s_first_data_block + target_extent->start + byte_index), 0, residue);
		} else if (residue < 0) {
			return -errno;
		} 

	} else {
		// check if we need to delete a complete block
		struct a1fs_extent *target_inode_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + target_inode->extend_pt);
		int data_remain = (target_inode->size) % A1FS_BLOCK_SIZE;
		int data_to_be_deleted = target_inode->size - size;
		if (data_to_be_deleted < data_remain){
			target_inode->size = size;
		}
		if (data_to_be_deleted >= data_remain){
			target_inode->size = size;
			//update databit map and extent
			int data_remain_2 = size % A1FS_BLOCK_SIZE;
			int full_blocks = size / A1FS_BLOCK_SIZE;
			struct a1fs_extent new_extent;
			new_extent.start = target_inode_extent->start;
			if (data_remain_2 != 0){
				new_extent.count = full_blocks + 1;
			} else {
				new_extent.count = full_blocks;
			}
			// remove the original data block bitmap
			rm_multiple_data_bitmap(image, sp, *target_inode_extent);
			// set new data block bitmap
			set_multiple_data_bitmap(image, sp, new_extent);
			return 0;
		}
	}

	return 0;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: read data from the file at given offset into the buffer
	// attribute from fs_ctx *fs
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	int target_inode_index;
	get_inode(path, image, sp, &target_inode_index);

	/** create target inode. Assume file only have one block of data. 
	 * which means if offset >= 4096, then assume it is beyond end of file.
	 * Modify later.
	*/
	struct a1fs_inode *target_inode = (struct a1fs_inode *)(image + sp->s_first_inode + target_inode_index * sizeof(a1fs_inode));
	if (offset >= (unsigned int)target_inode->size){
		return 0;
	}

	struct a1fs_extent *first_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + target_inode->extend_pt);
	int data_start = first_extent->start;

	size_t remain =  A1FS_BLOCK_SIZE - (offset % A1FS_BLOCK_SIZE);
	if (remain <= size){
		if (target_inode->size >= offset + size){
			memcpy(buf, (image + sp->s_first_data_block + data_start + offset), remain);
		}
		//if (target_inode->size < offset + size)
		else {
			size_t data_not_zero = target_inode->size - offset;
			memcpy(buf, (image + sp->s_first_data_block + data_start + offset), data_not_zero);
			// memset(buf + data_not_zero, 0, remain - target_inode->size % A1FS_BLOCK_SIZE);
		}
		
	} else {
		if (target_inode->size >= offset + size){
			memcpy(buf, (image + sp->s_first_data_block + data_start + offset), size); 
		} else {
			size_t data_not_zero = target_inode->size - offset;
			memcpy(buf, (image + sp->s_first_data_block + data_start + offset), data_not_zero);
			// size_t data_to_be_zero = offset + size - target_inode->size;
			// memset(buf + data_not_zero, 0, data_to_be_zero);
		}
	}
	return size;
}


/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
		
	void *image = fs->image;
	struct a1fs_superblock *sp = (struct a1fs_superblock *)(image);

	int target_inode;
	get_inode(path, image, sp, &target_inode);

	/** create target inode. Assume file only have one extent of data. 
	 * Modify later.
	*/
	struct a1fs_inode *target = (struct a1fs_inode *)(image + sp->s_first_inode + target_inode * sizeof(a1fs_inode));
	int new_size = size + offset;
	// calculate target data block size and the new datablock size after offset
	int target_blocks = (((target->size)%A1FS_BLOCK_SIZE)==0) ? target->size/A1FS_BLOCK_SIZE : target->size/A1FS_BLOCK_SIZE + 1;
	int size_blocks = ((new_size%A1FS_BLOCK_SIZE)==0) ? new_size/A1FS_BLOCK_SIZE : new_size/A1FS_BLOCK_SIZE + 1;


	if (target_blocks < size_blocks) {
		a1fs_truncate(path, new_size);
	}

	int extent_index1;
	int byte_index1;
	find_extent(image, sp, target, offset, &extent_index1, &byte_index1);

	struct a1fs_extent *offset_extent = (struct a1fs_extent *)(image + sp->s_first_data_block + target->extend_pt + extent_index1*sizeof(a1fs_extent));
	int residue = offset_extent->count * A1FS_BLOCK_SIZE - (byte_index1+1);
	if ((unsigned int)residue < (unsigned int)size) {
		int leftover = size - residue;
		memcpy((image + sp->s_first_data_block + offset_extent->start + offset), buf, residue);

		struct a1fs_extent *offset_extent2 = (struct a1fs_extent *)(image + sp->s_first_data_block + target->extend_pt + (extent_index1+1)*sizeof(a1fs_extent));
		memcpy((image + sp->s_first_data_block + offset_extent2->start), buf+residue, leftover);
	}
	else {
		memcpy((image + sp->s_first_data_block + offset_extent->start + offset), buf, size);
	}

	return size;
}


static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}