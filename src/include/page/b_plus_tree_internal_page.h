#ifndef MINISQL_B_PLUS_TREE_INTERNAL_PAGE_H
#define MINISQL_B_PLUS_TREE_INTERNAL_PAGE_H

#include <string.h>

#include <queue>

#include "index/generic_key.h"
#include "page/b_plus_tree_page.h"

#define INTERNAL_PAGE_HEADER_SIZE 28
/**
在内部页面中存储 n 个索引键和 n+1 个子指针（页 ID）。
指针 PAGE_ID(i) 指向一棵子树，其中所有键 K 都满足：
K(i) <= K < K(i+1)。
注意：由于键的数量不等于子指针的数量，
第一个键始终保持无效。也就是说，任何搜索/查找
都应该忽略第一个键。
内部页面格式（按键值递增顺序存储）：
| 头部 | 键(1)+页ID(1) | 键(2)+页ID(2) | ... | 键(n)+页ID(n) |
 */
class BPlusTreeInternalPage : public BPlusTreePage {
 public:
  // must call initialize method after "create" a new node
  void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int key_size = UNDEFINED_SIZE,
            int max_size = UNDEFINED_SIZE);

  GenericKey *KeyAt(int index);

  void SetKeyAt(int index, GenericKey *key);

  int ValueIndex(const page_id_t &value) const;

  page_id_t ValueAt(int index) const;

  void SetValueAt(int index, page_id_t value);

  void *PairPtrAt(int index);

  void PairCopy(void *dest, void *src, int pair_num = 1);

  page_id_t Lookup(const GenericKey *key, const KeyManager &KP);

  void PopulateNewRoot(const page_id_t &old_value, GenericKey *new_key, const page_id_t &new_value);

  int InsertNodeAfter(const page_id_t &old_value, GenericKey *new_key, const page_id_t &new_value);

  void Remove(int index);

  page_id_t RemoveAndReturnOnlyChild();

  // Split and Merge utility methods
  void MoveAllTo(BPlusTreeInternalPage *recipient, GenericKey *middle_key, BufferPoolManager *buffer_pool_manager);

  void MoveHalfTo(BPlusTreeInternalPage *recipient, BufferPoolManager *buffer_pool_manager);

  void MoveFirstToEndOf(BPlusTreeInternalPage *recipient, GenericKey *middle_key,
                        BufferPoolManager *buffer_pool_manager);

  void MoveLastToFrontOf(BPlusTreeInternalPage *recipient, GenericKey *middle_key,
                         BufferPoolManager *buffer_pool_manager);

 private:
  void CopyNFrom(void *src, int size, BufferPoolManager *buffer_pool_manager);

  void CopyLastFrom(GenericKey *key, page_id_t value, BufferPoolManager *buffer_pool_manager);

  void CopyFirstFrom(page_id_t value, BufferPoolManager *buffer_pool_manager);

  char data_[PAGE_SIZE - INTERNAL_PAGE_HEADER_SIZE];
};

using InternalPage = BPlusTreeInternalPage;
#endif  // MINISQL_B_PLUS_TREE_INTERNAL_PAGE_H
