#ifndef _JUZFS_H_
#define _JUZFS_H_

#define FUSE_USE_VERSION 26
#include "stdio.h"
#include "stdlib.h"
#include <unistd.h>
#include "fcntl.h"
#include "string.h"
#include "fuse.h"
#include <stddef.h>
#include "ddriver.h"
#include "errno.h"
#include "types.h"

/******************************************************************************
* SECTION: macro debug
*******************************************************************************/
#define SFS_DBG(fmt, ...) do { printf("SFS_DBG: " fmt, ##__VA_ARGS__); } while(0) 
/******************************************************************************
* SECTION: juzfs.c
*******************************************************************************/
void* 			   	juzfs_init(struct fuse_conn_info *);
void  			   	juzfs_destroy(void *);
int   			   	juzfs_mkdir(const char *, mode_t);
int   			   	juzfs_getattr(const char *, struct stat *);
int   			   	juzfs_readdir(const char *, void *, fuse_fill_dir_t, off_t,
						                struct fuse_file_info *);
int   			   	juzfs_mknod(const char *, mode_t, dev_t);
int   			   	juzfs_write(const char *, const char *, size_t, off_t,
					                  struct fuse_file_info *);
int   			   	juzfs_read(const char *, char *, size_t, off_t,
					                 struct fuse_file_info *);
int   			   	juzfs_access(const char *, int);
int   			   	juzfs_unlink(const char *);
int   			   	juzfs_rmdir(const char *);
int   			   	juzfs_rename(const char *, const char *);
int   			   	juzfs_utimens(const char *, const struct timespec tv[2]);
int   			   	juzfs_truncate(const char *, off_t);
			
int   			   	juzfs_open(const char *, struct fuse_file_info *);
int   			   	juzfs_opendir(const char *, struct fuse_file_info *);

/******************************************************************************
* SECTION: juzfs_util.c
*******************************************************************************/
int 			   	jfs_mount(struct custom_options);
int                	jfs_driver_read(int, uint8_t *, int);
int 				jfs_driver_write(int, uint8_t *, int);
struct juzfs_inode* jfs_alloc_inode(struct juzfs_dentry *);
int 				jfs_sync_inode(struct juzfs_inode *);
struct juzfs_inode* jfs_read_inode(struct juzfs_dentry *, int);
int 				jfs_alloc_dentry(struct juzfs_inode*, struct juzfs_dentry*, bool);
uint64_t  			jfs_alloc_data_blk();
struct juzfs_dentry*jfs_lookup(const char *, bool*, bool*);
int 				jfs_calc_lvl(const char *);
char* 				jfs_get_name(const char*);
struct juzfs_dentry*jfs_get_dentry(struct juzfs_inode *, int);
int 				jfs_umount(void);
int  				jfs_dealloc_data_blk(int);
int 				juzfs_drop_dentry(struct juzfs_inode *, struct juzfs_dentry *);
int 				juzfs_drop_inode(struct juzfs_inode *);

/******************************************************************************
* SECTION: juzfs_debug.c
*******************************************************************************/
void jfs_dump_map(void);

#endif  /* _juzfs_H_ */
