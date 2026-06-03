#ifndef DISK_MGR_H
#define DISK_MGR_H

#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>

#include "common/config.h"
#include "common/macros.h"
#include "page/bitmap_page.h"
#include "page/disk_file_meta_page.h"

/**
 * DiskManager 负责数据库内部页面的分配与回收。
 * 它执行页面与磁盘之间的读取和写入操作，
 * 在数据库管理系统（DBMS）的上下文中提供了一个逻辑文件层。
 * 磁盘页面存储格式：(空闲页位图大小 = PAGE_SIZE * 8，我们将其记为 N)
 * | 元数据页 | 空闲页位图 1 | 页 1 | 页 2 | ....
 * | 页 N | 空闲页位图 2 | 页 N+1 | ... | 页 2N | ... |
 */
class DiskManager {
 public:
  explicit DiskManager(const std::string &db_file);

  ~DiskManager() {
    if (!closed) {
      Close();
    }
  }

  /**
   * 从指定的 page_id 读取页面数据
   * 注意：page_id = 0 是预留给空闲页位图（Free Page Bitmap）使用的
   */
  void ReadPage(page_id_t logical_page_id, char *page_data);

  /**
   * 将数据写入指定的页面
   * 注意：page_id = 0 是预留给空闲页位图（Free Page Bitmap）使用的
   */
  void WritePage(page_id_t logical_page_id, const char *page_data);

    /**
   * 从磁盘中获取下一个可用的空闲页
   * @return 返回已分配页面的逻辑页 ID (logical page id)
   */
  page_id_t AllocatePage();

  /**
   * 释放指定页并重置位图
   * Free this page and reset bit map
   */
  void DeAllocatePage(page_id_t logical_page_id);

  /**
   * 判断指定页是否空闲
   */
  bool IsPageFree(page_id_t logical_page_id);

  /**
   * 关闭磁盘管理器与所有文件资源
   */
  void Close();

  /**
   * Get Meta Page
   * Note: Used only for debug
   */
  char *GetMetaData() { return meta_data_; }

  static constexpr size_t BITMAP_SIZE = BitmapPage<PAGE_SIZE>::GetMaxSupportedSize();

 private:
  /**
   * Helper function to get disk file size
   */
  int GetFileSize(const std::string &file_name);

  /**
   * Read physical page from disk
   */
  void ReadPhysicalPage(page_id_t physical_page_id, char *page_data);

  /**
   * Write data to physical page in disk
   */
  void WritePhysicalPage(page_id_t physical_page_id, const char *page_data);

  /**
   * Map logical page id to physical page id
   */
  page_id_t MapPageId(page_id_t logical_page_id);

 private:
  // stream to write db file
  std::fstream db_io_;
  std::string file_name_;
  // with multiple buffer pool instances, need to protect file access
  std::recursive_mutex db_io_latch_;
  bool closed{false};
  char meta_data_[PAGE_SIZE];
  char buffer_[PAGE_SIZE];
};

#endif