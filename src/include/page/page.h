#ifndef MINISQL_PAGE_H
#define MINISQL_PAGE_H

#include <cstring>
#include <iostream>
#include <shared_mutex>

#include "common/config.h"
#include "common/rwlatch.h"

/**
Page 是数据库系统中的基本存储单元。它为驻留在主存中的实际数据页提供了一个封装。此外，Page
还包含用于缓冲池管理器的记账信息，例如： 固定计数（pin count）、脏页标志（dirty flag）、页 ID 等等。
 */
class Page {
  // Page 内部包含了一些记账信息，这些信息应当仅与缓冲池管理器相关。
  friend class BufferPoolManager;

 public:
  DISALLOW_COPY(Page)

  /** 构造函数。将页面数据清零。*/
  Page() { ResetMemory(); }

  /** Default destructor. */
  ~Page() = default;

  /** @return 页面持有的实际数据*/
  inline char *GetData() { return data_; }

  /** @return the page id of this page */
  inline page_id_t GetPageId() { return page_id_; }

  /** @return the pin count of this page */
  inline int GetPinCount() { return pin_count_; }

  /** @return true if the page in memory has been modified from the page on disk, false otherwise */
  inline bool IsDirty() { return is_dirty_; }

  /** Acquire the page write latch. */
  inline void WLatch() { rwlatch_.WLock(); }

  /** Release the page write latch. */
  inline void WUnlatch() { rwlatch_.WUnlock(); }

  /** Acquire the page read latch. */
  inline void RLatch() { rwlatch_.RLock(); }

  /** Release the page read latch. */
  inline void RUnlatch() { rwlatch_.RUnlock(); }

  /** @return the page LSN. */
  inline lsn_t GetLSN() { return *reinterpret_cast<lsn_t *>(GetData() + OFFSET_LSN); }

  /** Sets the page LSN. */
  inline void SetLSN(lsn_t lsn) { memcpy(GetData() + OFFSET_LSN, &lsn, sizeof(lsn_t)); }

 protected:
  static_assert(sizeof(page_id_t) == 4);
  static_assert(sizeof(lsn_t) == 4);

  static constexpr size_t SIZE_PAGE_HEADER = 8;   // 页头大小8字节
  static constexpr size_t OFFSET_PAGE_START = 0;  // 页头起始偏移量0
  static constexpr size_t OFFSET_LSN = 4;         // LSN偏移量4字节

 private:
  /**将页面内数据清为0。*/
  inline void ResetMemory() { memset(data_, OFFSET_PAGE_START, PAGE_SIZE); }

  /** The actual data that is stored within a page. */
  char data_[PAGE_SIZE]{};
  /** The ID of this page. */
  page_id_t page_id_ = INVALID_PAGE_ID;
  /** The pin count of this page. */
  int pin_count_ = 0;
  /** True if the page is dirty, i.e. it is different from its corresponding page on disk. */
  bool is_dirty_ = false;
  /** Page latch. */
  ReaderWriterLatch rwlatch_;
};

#endif  // MINISQL_PAGE_H
