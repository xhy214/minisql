#ifndef MINISQL_B_PLUS_TREE_PAGE_H
#define MINISQL_B_PLUS_TREE_PAGE_H

#include <cassert>
#include <climits>
#include <cstdlib>
#include <string>

#include "buffer/buffer_pool_manager.h"

// define page type enum
enum class IndexPageType { INVALID_INDEX_PAGE = 0, LEAF_PAGE, INTERNAL_PAGE };

#define UNDEFINED_SIZE 0
/**
 * 内部页（Internal Page）和叶子页（Leaf Page）都继承自这个页面类。
 *
 * 它实际上充当了每个 B+ 树页面的头部部分，
 * 包含了叶子页和内部页所共有的信息。
 *
 * 头部格式（单位：字节，共 28 字节）：
 * ----------------------------------------------------------------------------
 * | PageType (4) | KeySize (4) | LSN (4) | CurrentSize (4) | MaxSize (4) |
 * ----------------------------------------------------------------------------
 * | ParentPageId (4) | PageId(4) |
 * ----------------------------------------------------------------------------
 */
class BPlusTreePage {
 public:
  bool IsLeafPage() const;

  bool IsRootPage() const;

  void SetPageType(IndexPageType page_type);

  int GetKeySize() const;

  void SetKeySize(int size);

  int GetSize() const;

  void SetSize(int size);

  void IncreaseSize(int amount);

  int GetMaxSize() const;

  void SetMaxSize(int max_size);

  int GetMinSize() const;

  page_id_t GetParentPageId() const;

  void SetParentPageId(page_id_t parent_page_id);

  page_id_t GetPageId() const;

  void SetPageId(page_id_t page_id);

  void SetLSN(lsn_t lsn = INVALID_LSN);

 private:
  // member variable, attributes that both internal and leaf page share
  IndexPageType page_type_;
  int key_size_;
  lsn_t lsn_;
  int size_;
  int max_size_;
  page_id_t parent_page_id_;
  page_id_t page_id_;
};

#endif  // MINISQL_B_PLUS_TREE_PAGE_H
