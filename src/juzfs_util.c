#include "juzfs.h"
#include "types.h"
#include <asm-generic/errno-base.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern struct juzfs_super super; 
extern struct custom_options juzfs_options;

int inode_cnt = 0;

/**
 * @brief 挂载sfs, Layout 如下
 * 
 * Layout
 * | BSIZE = 1024 B |
 * | Super(1) | Inode Map(1) | Block Map(1) | Inode List(1) | DATA(*) |
 * 
 * IO_SZ * 2 = BLK_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int jfs_mount(struct custom_options options){
    int                     ret = 0;
    int                     driver_fd;
    struct juzfs_super_d    juzfs_super_d; 
    struct juzfs_dentry*    root_dentry;
    struct juzfs_inode*     root_inode;

    int                 inode_num;
    int                 data_blk_num;
    uint64_t            map_data_blks;
    uint64_t            map_inode_blks;
    
    int                 super_blks;
    bool                is_init = false;

    super.is_mounted = false;

    // driver_fd = open(options.device, O_RDWR);
    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0) {
        return driver_fd;
    }

    super.fd = driver_fd;
    ddriver_ioctl(JFS_DRIVER(), IOC_REQ_DEVICE_SIZE,  &super.sz_disk);
    ddriver_ioctl(JFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &super.sz_io);
    
    root_dentry         = new_dentry("/", NULL,DIR_TYPE);
    root_dentry->ino    = JFS_ROOT_INO;

    if (jfs_driver_read(JFS_SUPER_OFS, (uint8_t *)(&juzfs_super_d), 
                        sizeof(struct juzfs_super_d)) != 0) {
        return -EIO;
    }   
                                                      /* 读取super */
    if (juzfs_super_d.magic != JFS_MAGIC) {     /* 幻数无 */
                                                      /* 估算各部分大小 */
        super_blks      = JFS_ROUND_UP(sizeof(struct juzfs_super_d), JFS_BLK_SZ()) / JFS_BLK_SZ();

        // 只用inode和数据填满分区的理论最大inode
        inode_num       =  JFS_DISK_SZ() / ((JFS_DATA_PER_FILE + JFS_INODE_PER_FILE) * JFS_BLK_SZ());

        map_inode_blks  = JFS_ROUND_UP(JFS_ROUND_UP(inode_num, UINT32_BITS), JFS_BLK_SZ()) 
                         / JFS_BLK_SZ();

        data_blk_num    = inode_num / JFS_INODE_PER_FILE * JFS_DATA_PER_FILE;

        map_data_blks   = JFS_ROUND_UP(JFS_ROUND_UP(data_blk_num, UINT32_BITS), JFS_BLK_SZ()) 
                         / JFS_BLK_SZ();

        // 考虑其他数据结构占用空间后的实际最大inode
        inode_num       =  (JFS_DISK_SZ() - (super_blks + map_inode_blks + map_data_blks) * JFS_BLK_SZ()) / ((JFS_DATA_PER_FILE + JFS_INODE_PER_FILE) * JFS_BLK_SZ());

        data_blk_num    = (inode_num / JFS_INODE_PER_FILE) * JFS_DATA_PER_FILE;
        
                                                      /* 布局layout */
        juzfs_super_d.max_ino           = inode_num; 
        juzfs_super_d.map_inode_blks    = map_inode_blks;
        juzfs_super_d.map_inode_offset  = JFS_SUPER_OFS + JFS_BLKS_SZ((uint64_t)super_blks);
        juzfs_super_d.max_data_blks     = data_blk_num;
        juzfs_super_d.map_data_blks     = map_data_blks;
        juzfs_super_d.map_data_offset   = juzfs_super_d.map_inode_offset + JFS_BLKS_SZ((uint64_t) map_inode_blks);
        juzfs_super_d.ino_list_blks     = inode_num;
        juzfs_super_d.ino_list_offset   = juzfs_super_d.map_data_offset + JFS_BLKS_SZ((uint64_t)map_data_blks);
        juzfs_super_d.data_offset       = juzfs_super_d.ino_list_offset + JFS_BLKS_SZ((uint64_t)inode_num);

        juzfs_super_d.sz_usage          = 0;
        is_init                         = true;
    }

    super.sz_usage          = juzfs_super_d.sz_usage;      /* 建立 in-memory 结构 */
    
    super.max_ino           = juzfs_super_d.max_ino;
    super.map_inode         = (uint8_t *)malloc(JFS_BLKS_SZ(juzfs_super_d.map_inode_blks));
    super.map_data          = (uint8_t *)malloc(JFS_BLKS_SZ(juzfs_super_d.map_data_blks));
    // super.inode_list = (struct juzfs_inode*)malloc(JFS_BLKS_SZ(juzfs_super_d.max_ino));
    super.map_inode_blks    = juzfs_super_d.map_inode_blks;
    super.map_inode_offset  = juzfs_super_d.map_inode_offset;

    super.max_data_blks     = juzfs_super_d.max_data_blks;
    super.map_data_blks     = juzfs_super_d.map_data_blks;
    super.map_data_offset   = juzfs_super_d.map_data_offset;

    super.ino_list_blks     = juzfs_super_d.ino_list_blks;
    super.ino_list_offset   = juzfs_super_d.ino_list_offset;

    super.data_offset       = juzfs_super_d.data_offset;

    if (jfs_driver_read(juzfs_super_d.map_inode_offset, (uint8_t *)(super.map_inode), 
                        JFS_BLKS_SZ(juzfs_super_d.map_inode_blks)) != 0) {
        return -EIO;
    }

    if (jfs_driver_read(juzfs_super_d.map_data_offset, (uint8_t *)(super.map_data), 
                        JFS_BLKS_SZ(juzfs_super_d.map_data_blks)) != 0) {
        return -EIO;
    }

    // if (jfs_driver_read(juzfs_super_d.map_inode_offset, (uint8_t *)(super.inode_list), 
    //                     JFS_BLKS_SZ(juzfs_super_d.map_inode_blks)) != 0) {
    //     return -1;
    // }

    if (is_init) {
        // 保证位图为空
        memset(super.map_inode,0,JFS_BLKS_SZ(juzfs_super_d.map_inode_blks));
        memset(super.map_data,0,JFS_BLKS_SZ(juzfs_super_d.map_data_blks));

        /* 分配根节点 */
        root_inode = jfs_alloc_inode(root_dentry);
        jfs_sync_inode(root_inode);
    }
    
    root_inode          = jfs_read_inode(root_dentry, JFS_ROOT_INO);
    root_dentry->inode  = root_inode;
    super.root_dentry   = root_dentry;
    super.is_mounted    = true;

    jfs_dump_map();

    return ret;
}

/**
 * @brief 驱动读
 * 
 * @param offset 
 * @param out_content 
 * @param size 
 * @return int 
 */
int jfs_driver_read(int offset, uint8_t *out_content, int size) {
    int      offset_aligned = JFS_ROUND_DOWN(offset, JFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = JFS_ROUND_UP((size + bias), JFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(JFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // read(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_read(JFS_DRIVER(), cur, JFS_IO_SZ());
        cur          += JFS_IO_SZ();
        size_aligned -= JFS_IO_SZ();   
    }
    memcpy(out_content, temp_content + bias, size);
    free(temp_content);
    return 0;
}

/**
 * @brief 驱动写
 * 
 * @param offset 
 * @param in_content 
 * @param size 
 * @return int 
 */
int jfs_driver_write(int offset, uint8_t *in_content, int size) {
    int      offset_aligned = JFS_ROUND_DOWN(offset, JFS_IO_SZ());
    int      bias           = offset - offset_aligned;
    int      size_aligned   = JFS_ROUND_UP((size + bias), JFS_IO_SZ());
    uint8_t* temp_content   = (uint8_t*)malloc(size_aligned);
    uint8_t* cur            = temp_content;

    jfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);
    
    // lseek(SFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(JFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        // write(SFS_DRIVER(), cur, SFS_IO_SZ());
        ddriver_write(JFS_DRIVER(), (char*)cur, JFS_IO_SZ());
        cur          += JFS_IO_SZ();
        size_aligned -= JFS_IO_SZ();   
    }

    free(temp_content);
    return 0;
}

/**
 * @brief 分配一个inode，占用位图
 * 
 * @param dentry 该dentry指向分配的inode
 * @return sfs_inode
 */
struct juzfs_inode* jfs_alloc_inode(struct juzfs_dentry * dentry) {
    struct juzfs_inode* inode;
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int ino_cursor  = 0;
    bool is_find_free_entry = false;

    for (byte_cursor = 0; byte_cursor < JFS_BLKS_SZ(super.map_inode_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                                                      /* 当前ino_cursor位置空闲 */
                super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                inode_cnt++;
                printf("allocated inode %s\n",dentry->name);
                is_find_free_entry = true;           
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == super.max_ino)
        return (void*)-ENOSPC;

    inode = (struct juzfs_inode*)malloc(sizeof(struct juzfs_inode));
    inode->ino  = ino_cursor; 
    inode->size = 0;
                                                      /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino   = inode->ino;
                                                      /* inode指回dentry */
    inode->dentry = dentry;
    
    inode->dir_cnt = 0;
    inode->dentrys = NULL;
    inode->dentrys_list_size = 0;
    
    memset(inode->data_offsets, 0, sizeof(uint64_t)*JFS_DATA_PER_FILE);

    return inode;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 * 
 * @param inode 
 * @return int 
 */
int jfs_sync_inode(struct juzfs_inode * inode) {
    struct juzfs_inode_d  inode_d;
    struct juzfs_dentry_d*  dentrys_d;
    // struct juzfs_dentry_d dentry_d;
    uint64_t offset;
    size_t dentrys_d_size;
    int ino             = inode->ino;
    int blk_cursor      = 0;

    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    memcpy(inode_d.data_offsets,inode->data_offsets,JFS_INODE_DATA_OFS_ARRAY_SIZE());
    
    if (jfs_driver_write(JFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct juzfs_inode_d)) != 0) {
        return -EIO;
    }

    if (JFS_IS_DIR(inode)) {

        dentrys_d_size  = sizeof(struct juzfs_dentry_d)*inode->dir_cnt;
        dentrys_d       = (struct juzfs_dentry_d*)malloc(dentrys_d_size);

        for (int i=0; i < inode->dir_cnt; i++) {
            memcpy(dentrys_d[i].name, inode->dentrys[i].name, sizeof(char)*MAX_NAME_LEN);
            dentrys_d[i].ino = inode->dentrys[i].ino;
            dentrys_d[i].ftype = inode->dentrys[i].ftype;
            if (inode->dentrys[i].inode != NULL) {
                jfs_sync_inode(inode->dentrys[i].inode);
            }
        }

        for (blk_cursor = 0; blk_cursor < inode->dir_cnt / JFS_DENTRYS_SEG_SIZE(); blk_cursor++) {              
            offset= JFS_DATA_OFS(inode->data_offsets[blk_cursor]);

            if (jfs_driver_write(offset, (uint8_t *)&dentrys_d[blk_cursor * JFS_DENTRYS_SEG_SIZE()], JFS_DENTRYS_SEG_SIZE()*sizeof(struct juzfs_dentry_d)) != 0) {
                return -EIO;                     
            }
        }

        free(dentrys_d);
    }
    
    return 0;
}

/**
 * @brief 
 * 
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct sfs_inode* 
 */
struct juzfs_inode* jfs_read_inode(struct juzfs_dentry * dentry, int ino) {
    struct juzfs_inode*     inode = (struct juzfs_inode*)malloc(sizeof(struct juzfs_inode));
    struct juzfs_inode_d    inode_d;
    struct juzfs_dentry*    sub_dentry;
    // struct juzfs_dentry_d dentry_d;
    struct juzfs_dentry_d*  dentrys_d;
    uint64_t                offset;
    size_t                  dentrys_d_size;
    int                     blk_cursor;
    // int    dir_cnt = 0, i;
    if (jfs_driver_read(JFS_INO_OFS(ino), (uint8_t *)&inode_d, sizeof(struct juzfs_inode_d)) != 0) {
        return NULL;
    }
    inode->ino      = inode_d.ino;
    inode->size     = inode_d.size;
    inode->dir_cnt  = 0;
    inode->dentry   = dentry;
    inode->dentrys  = NULL;
    inode->dentrys_list_size = 0;
    memcpy(inode->data_offsets, inode_d.data_offsets, JFS_INODE_DATA_OFS_ARRAY_SIZE());

    if (JFS_IS_DIR(inode)) {
        inode->dir_cnt  = inode_d.dir_cnt;
        dentrys_d_size  = sizeof(struct juzfs_dentry)*inode->dir_cnt;
        dentrys_d       = (struct juzfs_dentry_d*)malloc(dentrys_d_size);

        for (blk_cursor = 0; blk_cursor < inode->dir_cnt / JFS_DENTRYS_SEG_SIZE(); blk_cursor++){            
            offset = JFS_DATA_OFS(inode->data_offsets[blk_cursor]);

            if (jfs_driver_read(offset, (uint8_t *)&dentrys_d[blk_cursor * JFS_DENTRYS_SEG_SIZE()], JFS_DENTRYS_SEG_SIZE()*sizeof(struct juzfs_dentry_d)) != 0){
                return NULL;
            }
        }

        for (int i = 0; i < inode_d.dir_cnt; i++)
        {
            //copy dentrys
            sub_dentry = new_dentry(dentrys_d[i].name, inode->dentry, dentrys_d->ftype);

            sub_dentry->ino = dentrys_d[i].ino;

            jfs_alloc_dentry(inode, sub_dentry,false);
        }

        free(dentrys_d);
    }
    return inode;
}

/**
 * @brief 为一个inode分配子dentry，数组不足时扩展数组
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int jfs_alloc_dentry(struct juzfs_inode* inode, struct juzfs_dentry* dentry, bool alloc_d)
{
    struct juzfs_dentry* old_dentrys;
    int new_list_size;

    // 空间不足
    if (inode->dentrys_list_size < inode->dir_cnt+1) {

        old_dentrys = inode->dentrys;

        new_list_size = JFS_ROUND_UP(inode->dir_cnt+1,JFS_DENTRYS_SEG_SIZE());

        // 目录超过六块
        if (new_list_size / JFS_DENTRYS_SEG_SIZE() > JFS_DATA_PER_FILE) return -ENOSPC;

        if (alloc_d)
            inode->data_offsets[new_list_size / JFS_DENTRYS_SEG_SIZE() - 1] = jfs_alloc_data_blk();

        inode->dentrys = (struct juzfs_dentry*)malloc(sizeof(struct juzfs_dentry) * new_list_size);

        if (old_dentrys != NULL) {
            // 非空目录
            memcpy(inode->dentrys, old_dentrys, sizeof(struct juzfs_dentry)*inode->dir_cnt);
            free(old_dentrys);
        }

        inode->dentrys_list_size = new_list_size;
    }

    memcpy(&(inode->dentrys[inode->dir_cnt]),dentry,sizeof(struct juzfs_dentry));

    inode->dir_cnt++;

    return inode->dir_cnt;
} 

/**
 * @brief 分配一个数据块
 * 
 * @return uint64_t 数据块 offset
 */
uint64_t  jfs_alloc_data_blk(void)
{
    int byte_cursor = 0; 
    int bit_cursor  = 0; 
    int blk_cursor  = 0;
    bool is_find_free_entry = false;

    for (byte_cursor = 0; byte_cursor < JFS_BLKS_SZ(super.map_data_blks); 
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            if((super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0) {    
                /* 当前blk_cursor位置空闲 */
                super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = true;           
                break;
            }
            blk_cursor++;
        }
        if (is_find_free_entry) {
            break;
        }
    }

    if (!is_find_free_entry || blk_cursor == super.max_data_blks)
        return -ENOSPC;

    return blk_cursor;
}

/**
 * @brief 释放一个数据块
 * 
 * @return int
 */
int  jfs_dealloc_data_blk(int blk_num) {
    int byte_cursor = blk_num / UINT8_BITS; 
    int bit_cursor  = blk_num % UINT8_BITS;

    super.map_data[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));

    return 0;
}

/**
 * @brief 
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry 
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 * 
 * @param path 
 * @return struct juzfs_dentry* 
 */
struct juzfs_dentry* jfs_lookup(const char * path, bool* is_find, bool* is_root) {
    struct juzfs_dentry* dentry_cursor = super.root_dentry;
    struct juzfs_dentry* dentry_ret = NULL;
    struct juzfs_inode*  inode; 
    int   total_lvl = jfs_calc_lvl(path);
    int   lvl = 0;
    bool is_hit;
    char* fname = NULL;
    char* path_cpy = (char*)malloc(sizeof(path));
    *is_root = false;
    strcpy(path_cpy, path);

    if (total_lvl == 0) {                           /* 根目录 */
        *is_find = true;
        *is_root = true;
        dentry_ret = super.root_dentry;
    }
    fname = strtok(path_cpy, "/");       
    while (fname)
    {   
        lvl++;
        if (dentry_cursor->inode == NULL) {           /* Cache机制 */
            jfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (JFS_IS_FILE(inode) && lvl < total_lvl) {
            // SFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry;
            break;
        }
        if (JFS_IS_DIR(inode)) {
            dentry_cursor = inode->dentrys;
            is_hit        = false;

            for (int i = 0; i < inode->dir_cnt; i++)
            {
                dentry_cursor = &inode->dentrys[i];
                if (memcmp(dentry_cursor->name, fname, strlen(fname)) == 0) {
                    is_hit = true;
                    break;
                }
            }
            
            if (!is_hit) {
                *is_find = false;
                // SFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }

            if (is_hit && lvl == total_lvl) {
                *is_find = true;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        fname = strtok(NULL, "/"); 
    }

    free(path_cpy);

    if (dentry_ret->inode == NULL) {
        dentry_ret->inode = jfs_read_inode(dentry_ret, dentry_ret->ino);
    }
    
    return dentry_ret;
}

/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * /
 * -> lvl = 0
 * /a
 * -> lvl = 1
 * @param path 
 * @return int 
 */
int jfs_calc_lvl(const char * path) {
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char* str = path;
    int   lvl = 0;
    if (strcmp(path, "/") == 0) {
        return lvl;
    }
    while (*str != NULL) {
        if (*str == '/') {
            lvl++;
        }
        str++;
    }
    return lvl;
}

/**
 * @brief 获取文件名
 * 
 * @param path 
 * @return char* 
 */
char* jfs_get_name(const char* path) {
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}

/**
 * @brief 
 * 
 * @param inode 
 * @param dir [0...]
 * @return struct sfs_dentry* 
 */
struct juzfs_dentry* jfs_get_dentry(struct juzfs_inode * inode, int dir) {
    if (dir > inode->dir_cnt-1)
        return NULL;

    return &inode->dentrys[dir];
}

/**
 * @brief 
 * 
 * @return int 
 */
int jfs_umount(void) {
    struct juzfs_super_d  juzfs_super_d; 

    if (!super.is_mounted) {
        return 0;
    }

    jfs_sync_inode(super.root_dentry->inode);     /* 从根节点向下刷写节点 */
                                                    
    juzfs_super_d.magic               = JFS_MAGIC;
    juzfs_super_d.sz_usage            = super.sz_usage;

    juzfs_super_d.max_ino             = super.max_ino;
    juzfs_super_d.map_inode_blks      = super.map_inode_blks;
    juzfs_super_d.map_inode_offset    = super.map_inode_offset;
    juzfs_super_d.max_data_blks       = super.max_data_blks;
    juzfs_super_d.map_data_blks       = super.map_data_blks;
    juzfs_super_d.map_data_offset     = super.map_data_offset;
    juzfs_super_d.ino_list_blks       = super.ino_list_blks;
    juzfs_super_d.ino_list_offset     = super.ino_list_offset;
    juzfs_super_d.data_offset         = super.data_offset;

    if (jfs_driver_write(JFS_SUPER_OFS, (uint8_t *)&juzfs_super_d, 
                     sizeof(struct juzfs_super_d)) != 0) {
        return -EIO;
    }

    if (jfs_driver_write(juzfs_super_d.map_inode_offset, (uint8_t *)(super.map_inode), 
                         JFS_BLKS_SZ(juzfs_super_d.map_inode_blks)) != 0) {
        return -EIO;
    }

    if (jfs_driver_write(juzfs_super_d.map_data_offset, (uint8_t *)(super.map_data), 
                         JFS_BLKS_SZ(juzfs_super_d.map_data_blks)) != 0) {
        return -EIO;
    }

    free(super.map_inode);
    free(super.map_data);
    ddriver_close(JFS_DRIVER());

    printf("inode count=%d\n",inode_cnt);

    return 0;
}

/**
 * @brief 将dentry从inode的dentrys中取出
 * 
 * @param inode 
 * @param dentry 
 * @return int 
 */
int juzfs_drop_dentry(struct juzfs_inode * inode, struct juzfs_dentry * dentry) {
    struct juzfs_dentry* old_dentrys;
    int new_list_size;
    bool is_find = false;
    int dentry_cursor;
    
    for (dentry_cursor=0; dentry_cursor < inode->dir_cnt;dentry_cursor++) {
        if (strcmp(inode->dentrys[dentry_cursor].name,dentry->name) == 0) {
            is_find = true;
            break;
        }
    }
    if (!is_find) {
        return -ENOENT;
    }

    //将要删除的dentry之后的dentry往前移
    if (dentry_cursor != inode->dir_cnt-1) 
        memcpy(&(inode->dentrys[dentry_cursor]),&(inode->dentrys[dentry_cursor+1]),sizeof(struct juzfs_dentry) * (inode->dir_cnt - dentry_cursor - 1));

    old_dentrys = inode->dentrys;
    new_list_size = JFS_ROUND_UP((inode->dir_cnt-1),JFS_DENTRYS_SEG_SIZE());
    inode->dir_cnt--;

    int a = new_list_size/JFS_DENTRYS_SEG_SIZE();
    for(int i = inode->dentrys_list_size/JFS_DENTRYS_SEG_SIZE()-1; i >= a; i--){
        jfs_dealloc_data_blk(inode->data_offsets[i]);
    }

    if (inode->dentrys_list_size/JFS_DENTRYS_SEG_SIZE() != new_list_size/JFS_DENTRYS_SEG_SIZE() && inode->dir_cnt != 0) {
        inode->dentrys = (struct juzfs_dentry*)malloc(sizeof(struct juzfs_dentry) * new_list_size);
        memcpy(inode->dentrys, old_dentrys, sizeof(struct juzfs_dentry)*inode->dir_cnt);
        free(old_dentrys);
    }

    if(inode->dir_cnt == 0) {
        inode->dentrys = NULL;
    }

    inode->dentrys_list_size = new_list_size;
    
    return inode->dir_cnt;
}

/**
 * @brief 删除内存中的一个inode， 暂时不释放
 * Case 1: Reg File
 * 
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Reg Dentry)
 *                       |
 *                      Inode  (Reg File)
 * 
 *  1) Step 1. Erase Bitmap     
 *  2) Step 2. Free Inode                      (Function of sfs_drop_inode)
 * ------------------------------------------------------------------------
 *  3) *Setp 3. Free Dentry belonging to Inode (Outsider)
 * ========================================================================
 * Case 2: Dir
 *                  Inode
 *                /      \
 *            Dentry -> Dentry (Dir Dentry)
 *                       |
 *                      Inode  (Dir)
 *                    /     \
 *                Dentry -> Dentry
 * 
 *   Recursive
 * @param inode 
 * @return int 
 */
int juzfs_drop_inode(struct juzfs_inode * inode) {
    struct juzfs_dentry*  dentry_cursor;
    struct juzfs_dentry*  dentry_to_free;
    struct juzfs_inode*   inode_cursor;

    int byte_cursor; 
    int bit_cursor; 
    bool is_find = false;
    int data_blks;

    if (inode == super.root_dentry->inode) {
        return -EINVAL;
    }

    /* 调整inodemap */
    byte_cursor = (int)(inode->ino)/UINT8_BITS;
    bit_cursor  = (int)(inode->ino)%UINT8_BITS;
    super.map_inode[byte_cursor] &= (uint8_t)(~(0x1 << bit_cursor));

    if (JFS_IS_DIR(inode)) {
        for(int i = 0; i<inode->dir_cnt;i++)
        {   
            dentry_cursor = &(inode->dentrys[i]);
            inode_cursor = dentry_cursor->inode;
            juzfs_drop_inode(inode_cursor);
            juzfs_drop_dentry(inode, dentry_cursor);
        }
    }
    else if (JFS_IS_FILE(inode)) {
        data_blks = JFS_ROUND_UP(inode->size,JFS_BLK_SZ() / JFS_BLK_SZ());

        if (inode->data_offsets){
            for (int i = 0; i < data_blks; i++) {
                jfs_dealloc_data_blk(inode->data_offsets[i]);
            }
        }
    }
    
    free(inode);
    return 0;
}
