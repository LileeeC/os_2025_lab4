#include <linux/fs.h>
#include <linux/uaccess.h>
#include "osfs.h"

/**
 * Function: osfs_read
 * Description: Reads data from a file.
 * Inputs:
 *   - filp: The file pointer representing the file to read from.
 *   - buf: The user-space buffer to copy the data into.
 *   - len: The number of bytes to read.
 *   - ppos: The file position pointer.
 * Returns:
 *   - The number of bytes read on success.
 *   - 0 if the end of the file is reached.
 *   - -EFAULT if copying data to user space fails.
 */
static ssize_t osfs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    void *data_block;
    ssize_t bytes_read;

    // If the file has not been allocated a data block, it indicates the file is empty
    if (osfs_inode->i_blocks == 0)
        return 0;

    if (*ppos >= osfs_inode->i_size)
        return 0;

    if (*ppos + len > osfs_inode->i_size)
        len = osfs_inode->i_size - *ppos;

    data_block = sb_info->data_blocks + osfs_inode->i_block * BLOCK_SIZE + *ppos;
    if (copy_to_user(buf, data_block, len))
        return -EFAULT;

    *ppos += len;
    bytes_read = len;

    return bytes_read;
}


/**
 * Function: osfs_write
 * Description: Writes data to a file.
 * Inputs:
 *   - filp: The file pointer representing the file to write to.
 *   - buf: The user-space buffer containing the data to write.
 *   - len: The number of bytes to write.
 *   - ppos: The file position pointer.
 * Returns:
 *   - The number of bytes written on success.
 *   - -EFAULT if copying data from user space fails.
 *   - Adjusted length if the write exceeds the block size.
 */
static ssize_t osfs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{   
    //Step1: Retrieve the inode and filesystem information
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    void *data_block;
    ssize_t bytes_written;
    int ret;

    // Step2: Check if a data block has been allocated; if not, allocate one
    if (osfs_inode->i_blocks == 0) {
        ret = osfs_alloc_data_block(sb_info, &osfs_inode->i_block);
        if (ret) {
            return ret; // 空間不足 (-ENOSPC)
        }
        osfs_inode->i_blocks = 1;
        // 將新分配的區塊清零，避免讀到舊的垃圾資料
        memset(sb_info->data_blocks + osfs_inode->i_block * BLOCK_SIZE, 0, BLOCK_SIZE);
    }

    // Step3: Limit the write length to fit within one data block
    // 因為這個簡單的 FS 每個檔案只有一個 Block，寫入位置不能超過 4096
    if (*ppos >= BLOCK_SIZE) {
        return -ENOSPC; 
    }
    // 如果欲寫入的長度超過了剩餘空間，將長度截斷
    if (*ppos + len > BLOCK_SIZE) {
        len = BLOCK_SIZE - *ppos;
    }

    // Step4: Write data from user space to the data block
    // 記憶體位址：基底位址 (data_blocks) + 區塊偏移 (block index * 4096) + 區塊內偏移 (*ppos)
    data_block = sb_info->data_blocks + osfs_inode->i_block * BLOCK_SIZE + *ppos;
    
    // 使用 copy_from_user 安全地複製資料
    if (copy_from_user(data_block, buf, len)) {
        return -EFAULT;
    }

    // Step5: Update inode & osfs_inode attribute
    *ppos += len; // 更新讀寫頭位置
    bytes_written = len;

    // 如果寫入導致檔案變大，更新檔案大小
    if (*ppos > osfs_inode->i_size) {
        osfs_inode->i_size = *ppos;
        inode->i_size = *ppos; // 重要：也要更新 VFS 的 inode 讓 ls 指令看得到變化
    }

    // 更新檔案修改時間
    inode_set_mtime_to_ts(inode, current_time(inode));
    inode_set_ctime_to_ts(inode, current_time(inode));
    mark_inode_dirty(inode); // 標記 dirty

    // Step6: Return the number of bytes written
    return bytes_written;
}

/**
 * Struct: osfs_file_operations
 * Description: Defines the file operations for regular files in osfs.
 */
const struct file_operations osfs_file_operations = {
    .open = generic_file_open, // Use generic open or implement osfs_open if needed
    .read = osfs_read,
    .write = osfs_write,
    .llseek = default_llseek,
    // Add other operations as needed
};

/**
 * Struct: osfs_file_inode_operations
 * Description: Defines the inode operations for regular files in osfs.
 * Note: Add additional operations such as getattr as needed.
 */
const struct inode_operations osfs_file_inode_operations = {
    // Add inode operations here, e.g., .getattr = osfs_getattr,
};
