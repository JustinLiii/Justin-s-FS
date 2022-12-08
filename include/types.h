#ifndef _TYPES_H_
#define _TYPES_H_

#include "metadef.h"
#include <stdint.h>

   

/******************************************************************************
* SECTION: Type def
*******************************************************************************/
typedef enum jfs_file_type {
    FILE_TYPE,
    DIR_TYPE
} JFS_FILE_TYPE ;

struct custom_options {
	const char*        device;
};

/******************************************************************************
* SECTION: Macros
*******************************************************************************/
#define MAX_NAME_LEN    128  
#define UINT32_BITS             32
#define UINT8_BITS              8

#define JFS_MAGIC           0x114514
#define JFS_DEFAULT_PERM    0777   /* 全权限打开 */
#define JFS_SUPER_OFS           (uint64_t)0

#define JFS_ROOT_INO            0

#define JFS_INODE_PER_FILE      1
#define JFS_DATA_PER_FILE       6

#define JFS_DENTRYS_SEG_SIZE    8

// #define SFS_IOC_MAGIC           'S'
// #define SFS_IOC_SEEK            _IO(SFS_IOC_MAGIC, 0)

// #define SFS_FLAG_BUF_DIRTY      0x1
// #define SFS_FLAG_BUF_OCCUPY     0x2

/******************************************************************************
* SECTION: Macro Functions
*******************************************************************************/
#define JFS_IO_SZ()                     (super.sz_io)
#define JFS_BLK_SZ()                    (JFS_IO_SZ() * (uint64_t)2)
#define JFS_DISK_SZ()                   (super.sz_disk)
#define JFS_DRIVER()                    (super.fd)

#define JFS_INODE_DATA_OFS_ARRAY_SIZE() (sizeof(uint64_t)*JFS_DATA_PER_FILE)

#define JFS_ROUND_DOWN(value, round)    (value % round == 0 ? value : (value / round) * round)
#define JFS_ROUND_UP(value, round)      (value % round == 0 ? value : (value / round + (uint64_t)1) * round)

#define JFS_BLKS_SZ(blks)               (blks * JFS_BLK_SZ())

#define JFS_MAP_INO_OFS()                (super.map_inode_offset * JFS_BLK_SZ())
#define JFS_INO_OFS(ino)                ((super.ino_list_offset + ino) * JFS_BLK_SZ())
#define JFS_DATA_OFS(blkno)               (super.data_offset + JFS_BLKS_SZ(blkno))

#define JFS_IS_DIR(pinode)              (pinode->dentry->ftype == DIR_TYPE)
#define JFS_IS_FILE(pinode)              (pinode->dentry->ftype == FILE_TYPE)
#define JFS_ASSIGN_NAME(dentry, _name) memcpy(dentry->name, _name, strlen(_name))

/******************************************************************************
* SECTION: Structure - In memory
*******************************************************************************/

/**
* 注意：offset均用块表示
*/
struct juzfs_super {
    uint32_t            magic;
    int                 fd;  //only in mem
    
    int                 sz_io;  // io大小 only in mem
    int                 sz_disk; //only in mem
    int                 sz_usage;
    
    int                 max_ino;
    uint64_t            map_inode_blks;
    uint64_t            map_inode_offset;
    uint8_t*            map_inode; //only in mem

    int                 max_data_blks;
    uint64_t            map_data_blks;
    uint64_t            map_data_offset;
    uint8_t*            map_data; //only in mem

    uint64_t            ino_list_blks;
    uint64_t            ino_list_offset;
    // struct juzfs_inode* inode_list; //only in mem
    
    uint64_t            data_offset;

    bool                is_mounted; //only in mem

    struct juzfs_dentry* root_dentry; //only in mem
};

struct juzfs_inode {
    uint32_t                ino;
    int                     size;                           /* 文件已占用空间 */ //handled by func
    // char                 target_path[SFS_MAX_FILE_NAME]; /* store traget path when it is a symlink */
    int                     dir_cnt;
    struct juzfs_dentry*    dentry;                         /* 指向该inode的dentry */

    // arranged by func
    struct juzfs_dentry*    dentrys;                        /* 所有目录项 */
    int                     dentrys_list_size;


    uint64_t                data_offsets[JFS_DATA_PER_FILE];// size = 6
};

struct juzfs_dentry {
    char                    name[MAX_NAME_LEN];
    uint32_t                ino;
    struct juzfs_dentry*    parent;                        /* 父亲Inode的dentry */
    // struct sfs_dentry* brother;                       /* 兄弟 */
    struct juzfs_inode*     inode;                         /* 指向inode */
    JFS_FILE_TYPE           ftype;
};

//好用的初始化函数
static inline struct juzfs_dentry* new_dentry(char * name, struct juzfs_dentry* parent, JFS_FILE_TYPE ftype) {
    struct juzfs_dentry * dentry = (struct juzfs_dentry *)malloc(sizeof(struct juzfs_dentry));
    memset(dentry, 0, sizeof(struct juzfs_dentry));
    JFS_ASSIGN_NAME(dentry, name);
    dentry->ftype   = ftype;
    dentry->ino     = -1;
    dentry->inode   = NULL;
    dentry->parent  = parent;
    return dentry;
}

/******************************************************************************
* SECTION: Structure - In disk
*******************************************************************************/
struct juzfs_super_d {
    uint32_t        magic;
    int             sz_usage;
    
    int             max_ino;
    uint64_t        map_inode_blks;
    uint64_t        map_inode_offset;
    int             max_data_blks;
    uint64_t        map_data_blks;
    uint64_t        map_data_offset;
    uint64_t        ino_list_blks;
    uint64_t        ino_list_offset;
    uint64_t        data_offset;
};

struct juzfs_inode_d {
    uint32_t        ino;
    int             size;     
    int             dir_cnt;
    JFS_FILE_TYPE   ftype;
    uint64_t        data_offsets[JFS_DATA_PER_FILE];    // size = 6
};

struct juzfs_dentry_d {
    char            name[MAX_NAME_LEN];
    uint32_t        ino;
    JFS_FILE_TYPE   ftype;
};

#endif /* _TYPES_H_ */
