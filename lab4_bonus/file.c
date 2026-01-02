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
    ssize_t bytes_read = 0;
    
    if (*ppos >= osfs_inode->i_size)
        return 0;

    if (*ppos + len > osfs_inode->i_size)
        len = osfs_inode->i_size - *ppos;

    while (len > 0) {
        // 1. 計算目前讀寫頭在哪一個 "邏輯 block" (第幾個格子)
        uint32_t block_index = *ppos / BLOCK_SIZE;
        // 2. 計算在該 block 內的偏移量
        uint32_t offset_in_block = *ppos % BLOCK_SIZE;
        // 3. 計算這次迴圈能讀多少 (不能超過目前 block 的剩餘空間)
        size_t copy_len = BLOCK_SIZE - offset_in_block;
        if (copy_len > len) copy_len = len;

        // 4. 取得 "物理 block" 編號
        uint32_t phy_block_no = osfs_inode->blocks[block_index];
        
        // 算出記憶體位置
        data_block = sb_info->data_blocks + phy_block_no * BLOCK_SIZE + offset_in_block;

        // 複製給使用者
        if (copy_to_user(buf, data_block, copy_len))
            return -EFAULT;

        // 更新變數，準備跑下一個 block (如果需要的話)
        buf += copy_len;
        *ppos += copy_len;
        len -= copy_len;
        bytes_read += copy_len;
    }

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
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    void *data_block;
    ssize_t bytes_written = 0;
    int ret;

    while (len > 0) {
        // 1. 計算目前在哪個邏輯 block
        uint32_t block_index = *ppos / BLOCK_SIZE;
        uint32_t offset_in_block = *ppos % BLOCK_SIZE;
        size_t copy_len = BLOCK_SIZE - offset_in_block;
        if (copy_len > len) copy_len = len;

        // 2. 檢查是否超過檔案大小限制 (Bonus 限制最多 5 個 blocks)
        if (block_index >= MAX_BLOCKS_PER_FILE) {
            if (bytes_written > 0) return bytes_written; // 已經寫了一些就回傳成功的部分
            return -ENOSPC; // 空間不足
        }

        // 3. 如果這個 block 還沒分配，就分配一個
        if (osfs_inode->blocks[block_index] == 0) {
             // 這裡做一個簡單檢查：如果是 Block 0 但值是 0，可能是剛初始化。
             // 嚴謹一點應該用 i_blocks 計數，但這邊簡化處理：
             // 只要該 slot 是 0，就代表還沒分配實體空間
            ret = osfs_alloc_data_block(sb_info, &osfs_inode->blocks[block_index]);
            if (ret) {
                if (bytes_written > 0) return bytes_written;
                return ret; 
            }
            osfs_inode->i_blocks++;
            // 清空新分配的記憶體
            memset(sb_info->data_blocks + osfs_inode->blocks[block_index] * BLOCK_SIZE, 0, BLOCK_SIZE);
        }

        // 4. 寫入資料
        data_block = sb_info->data_blocks + osfs_inode->blocks[block_index] * BLOCK_SIZE + offset_in_block;
        
        if (copy_from_user(data_block, buf, copy_len)) {
            return -EFAULT;
        }

        // 5. 更新狀態
        buf += copy_len;
        *ppos += copy_len;
        len -= copy_len;
        bytes_written += copy_len;

        // 更新檔案大小
        if (*ppos > osfs_inode->i_size) {
            osfs_inode->i_size = *ppos;
            inode->i_size = *ppos;
        }
    }
    
    // 更新時間
    inode_set_mtime_to_ts(inode, current_time(inode));
    inode_set_ctime_to_ts(inode, current_time(inode));
    mark_inode_dirty(inode);

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
