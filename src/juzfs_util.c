#include "juzfs.h"
#include "types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern struct juzfs_super super; 
extern struct custom_options juzfs_options;

/**
 * @brief 挂载sfs, Layout 如下
 * 
 * Layout
 * | BSIZE = 1024 B |
 * | Super(1) | Inode Map(1) | Inode List(1) | DATA(*) |
 * 
 * IO_SZ * 2 = BLK_SZ
 * 
 * 每个Inode占用一个Blk
 * @param options 
 * @return int 
 */
int jfs_mount(struct custom_options options){
    int                 ret = -1;
    int                 driver_fd;
    struct juzfs_super_d  juzfs_super_d; 
    struct juzfs_dentry*  root_dentry;
    struct juzfs_inode*   root_inode;

    int                 inode_num;
    int                 map_inode_blks;
    
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
    
    root_dentry = new_dentry("/", NULL,DIR_TYPE);

    if (jfs_driver_read(JFS_SUPER_OFS, (uint8_t *)(&juzfs_super_d), 
                        sizeof(struct juzfs_super_d)) != 0) {
        return -1;
    }   
                                                      /* 读取super */
    if (juzfs_super_d.magic != JFS_MAGIC) {     /* 幻数无 */
                                                      /* 估算各部分大小 */
        super_blks = JFS_ROUND_UP(sizeof(struct juzfs_super_d), JFS_BLK_SZ()) / JFS_BLK_SZ();

        inode_num  =  JFS_DISK_SZ() / ((JFS_DATA_PER_FILE + JFS_INODE_PER_FILE) * JFS_BLK_SZ());

        map_inode_blks = JFS_ROUND_UP(JFS_ROUND_UP(inode_num, UINT32_BITS), JFS_BLK_SZ()) 
                         / JFS_BLK_SZ();

        inode_num  =  JFS_DISK_SZ() / ((JFS_DATA_PER_FILE + JFS_INODE_PER_FILE + super_blks + map_inode_blks) * JFS_BLK_SZ());
        
                                                      /* 布局layout */
        juzfs_super_d.max_ino = inode_num; 
        juzfs_super_d.map_inode_offset = JFS_SUPER_OFS + JFS_BLKS_SZ((uint64_t)super_blks);
        juzfs_super_d.map_inode_blks  = map_inode_blks;
        juzfs_super_d.ino_list_blks = inode_num;
        juzfs_super_d.ino_list_offset = super.map_inode_offset + JFS_BLKS_SZ((uint64_t)map_inode_blks);
        juzfs_super_d.data_offset = super.ino_list_offset + JFS_BLKS_SZ((uint64_t)inode_num);

        juzfs_super_d.sz_usage    = 0;
        is_init = true;
    }

    super.sz_usage   = juzfs_super_d.sz_usage;      /* 建立 in-memory 结构 */
    
    super.max_ino = juzfs_super_d.max_ino;
    super.map_inode = (uint8_t *)malloc(JFS_BLKS_SZ(juzfs_super_d.map_inode_blks));
    super.inode_list = (struct juzfs_inode*)malloc(JFS_BLKS_SZ(juzfs_super_d.max_ino));
    super.map_inode_blks = juzfs_super_d.map_inode_blks;
    super.map_inode_offset = juzfs_super_d.map_inode_offset;

    super.ino_list_blks = juzfs_super_d.ino_list_blks;
    super.ino_list_offset = juzfs_super_d.ino_list_offset;

    super.data_offset = juzfs_super_d.data_offset;

    if (jfs_driver_read(juzfs_super_d.map_inode_offset, (uint8_t *)(super.map_inode), 
                        JFS_BLKS_SZ(juzfs_super_d.map_inode_blks)) != 0) {
        return -1;
    }

    if (is_init) {                                    /* 分配根节点 */
        root_inode = jfs_alloc_inode(root_dentry);
        jfs_sync_inode(root_inode);
    }
    
    root_inode            = sfs_read_inode(root_dentry, SFS_ROOT_INO);
    root_dentry->inode    = root_inode;
    sfs_super.root_dentry = root_dentry;
    sfs_super.is_mounted  = TRUE;

    sfs_dump_map();
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
        ddriver_write(JFS_DRIVER(), cur, JFS_IO_SZ());
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
        return NULL;

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
    struct juzfs_dentry*  dentrys_d;
    struct juzfs_dentry_d dentry_d;
    uint64_t offset;
    size_t dentrys_d_size;
    int ino             = inode->ino;
    inode_d.ino         = ino;
    inode_d.size        = inode->size;
    inode_d.ftype       = inode->dentry->ftype;
    inode_d.dir_cnt     = inode->dir_cnt;
    memcpy(inode_d.data_offsets,inode->data_offsets,sizeof(uint64_t)*JFS_DATA_PER_FILE);
    
    if (jfs_driver_write(JFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                     sizeof(struct juzfs_inode_d)) != 0) {
        return -1;
    }

    if (JFS_IS_DIR(inode)) {
        dentrys_d_size = sizeof(struct juzfs_dentry)*inode->dir_cnt;
        dentrys_d   = (struct juzfs_dentry*)malloc(dentrys_d_size);                   
        offset      = inode->data_offsets[0];

        memcpy(dentrys_d, inode->dentrys, dentrys_d_size);

        if (jfs_driver_write(offset, (uint8_t *)&dentry_d, dentrys_d_size) != 0) {
            return -1;                     
        }
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
struct jfs_inode* jfs_read_inode(struct juzfs_dentry * dentry, int ino) {
    struct juzfs_inode* inode = (struct juzfs_inode*)malloc(sizeof(struct juzfs_inode));
    struct sfs_inode_d inode_d;
    struct sfs_dentry* sub_dentry;
    struct sfs_dentry_d dentry_d;
    int    dir_cnt = 0, i;
    if (sfs_driver_read(SFS_INO_OFS(ino), (uint8_t *)&inode_d, 
                        sizeof(struct sfs_inode_d)) != SFS_ERROR_NONE) {
        SFS_DBG("[%s] io error\n", __func__);
        return NULL;                    
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    memcpy(inode->target_path, inode_d.target_path, SFS_MAX_FILE_NAME);
    inode->dentry = dentry;
    inode->dentrys = NULL;
    if (SFS_IS_DIR(inode)) {
        dir_cnt = inode_d.dir_cnt;
        for (i = 0; i < dir_cnt; i++)
        {
            if (sfs_driver_read(SFS_DATA_OFS(ino) + i * sizeof(struct sfs_dentry_d), 
                                (uint8_t *)&dentry_d, 
                                sizeof(struct sfs_dentry_d)) != SFS_ERROR_NONE) {
                SFS_DBG("[%s] io error\n", __func__);
                return NULL;                    
            }
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino    = dentry_d.ino; 
            sfs_alloc_dentry(inode, sub_dentry);
        }
    }
    else if (SFS_IS_REG(inode)) {
        inode->data = (uint8_t *)malloc(SFS_BLKS_SZ(SFS_DATA_PER_FILE));
        if (sfs_driver_read(SFS_DATA_OFS(ino), (uint8_t *)inode->data, 
                            SFS_BLKS_SZ(SFS_DATA_PER_FILE)) != SFS_ERROR_NONE) {
            SFS_DBG("[%s] io error\n", __func__);
            return NULL;                    
        }
    }
    return inode;
}
