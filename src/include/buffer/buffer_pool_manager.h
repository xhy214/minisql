#ifndef MINISQL_BUFFER_POOL_MANAGER_H
#define MINISQL_BUFFER_POOL_MANAGER_H

#include <list>
#include <mutex>
#include <unordered_map>

#include "buffer/lru_replacer.h"
#include "page/disk_file_meta_page.h"
#include "page/page.h"
#include "storage/disk_manager.h"

using namespace std;

class BufferPoolManager {
 public:
  explicit BufferPoolManager(size_t pool_size, DiskManager *disk_manager);

  ~BufferPoolManager();

  Page *FetchPage(page_id_t page_id);

  bool UnpinPage(page_id_t page_id, bool is_dirty);

  bool FlushPage(page_id_t page_id);

  Page *NewPage(page_id_t &page_id);

  bool DeletePage(page_id_t page_id);

  bool IsPageFree(page_id_t page_id);

  bool CheckAllUnpinned();

 private:
  /**
   * Allocate new page (operations like create index/table) For now just keep an increasing counter
   */
  page_id_t AllocatePage();

  /**
   * Deallocate page (operations like drop index/table) Need bitmap in header page for tracking pages
   */
  void DeallocatePage(page_id_t page_id);

  frame_id_t TryToFindFreePage();

 private:
  size_t pool_size_;                                 // 缓冲池中页面的总数量
  Page *pages_;                                      // 指向页面数组的指针
  DiskManager *disk_manager_;                        // 磁盘管理器的指针
  unordered_map<page_id_t, frame_id_t> page_table_;  // 页表，用于记录“页ID”到“帧ID”的映射，以便快速查找
  Replacer *replacer_;                               // 替换策略指针
  list<frame_id_t> free_list_;                       // 空闲列表，用于记录当前未被使用的空闲帧
  recursive_mutex latch_;                            // 递归互斥锁，用于保护上述共享数据结构，防止多线程并发冲突
};

#endif  // MINISQL_BUFFER_POOL_MANAGER_H
