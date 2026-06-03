#include "page/b_plus_tree_internal_page.h"

#include "index/generic_key.h"

#define pairs_off (data_)
#define pair_size (GetKeySize() + sizeof(page_id_t))
#define key_off 0
#define val_off GetKeySize()

/**
**
TODO: 学生实现
*辅助方法与工具函数
创建新内部页面后的初始化方法
包括设置页面类型、当前大小、页面 ID、父页面 ID 以及最大页面容量
*/
void InternalPage::Init(page_id_t page_id, page_id_t parent_id, int key_size, int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(0);
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetKeySize(key_size);
  SetMaxSize(max_size);
}
/*
 用于获取/设置与输入 "index"（即数组偏移量）相关联的键的辅助方法
 */
GenericKey *InternalPage::KeyAt(int index) {
  return reinterpret_cast<GenericKey *>(pairs_off + index * pair_size + key_off);
}

void InternalPage::SetKeyAt(int index, GenericKey *key) {
  memcpy(pairs_off + index * pair_size + key_off, key, GetKeySize());
}

page_id_t InternalPage::ValueAt(int index) const {
  return *reinterpret_cast<const page_id_t *>(pairs_off + index * pair_size + val_off);
}

void InternalPage::SetValueAt(int index, page_id_t value) {
  *reinterpret_cast<page_id_t *>(pairs_off + index * pair_size + val_off) = value;
}

int InternalPage::ValueIndex(const page_id_t &value) const {
  for (int i = 0; i < GetSize(); ++i) {
    if (ValueAt(i) == value) return i;
  }
  return -1;
}

void *InternalPage::PairPtrAt(int index) { return KeyAt(index); }

void InternalPage::PairCopy(void *dest, void *src, int pair_num) {
  memcpy(dest, src, pair_num * (GetKeySize() + sizeof(page_id_t)));
}
/*****************************************************************************
 * LOOKUP
 *****************************************************************************/
/*
 * Find and return the child pointer(page_id) which points to the child page
 * that contains input "key"
 * Start the search from the second key(the first key should always be invalid)
 */
page_id_t InternalPage::Lookup(const GenericKey *key, const KeyManager &KM) {  // 返回包含该key的子页面
  ASSERT(GetSize() > 0, "Internal page size must be greater than 0");
  if (GetSize() == 1) {
    return ValueAt(0);
  }
  int left = 1;
  int right = GetSize() - 1;
  int pos = GetSize();
  while (left <= right) {
    int mid = left + (right - left) / 2;
    if (KM.CompareKeys(key, KeyAt(mid)) < 0) {
      pos = mid;
      right = mid - 1;
    } else {
      left = mid + 1;
    }
  }
  return ValueAt(pos - 1);
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Populate new root page with old_value + new_key & new_value
 * When the insertion cause overflow from leaf page all the way upto the root
 * page, you should create a new root page and populate its elements.
 * NOTE: This method is only called within InsertIntoParent()(b_plus_tree.cpp)
 */
void InternalPage::PopulateNewRoot(const page_id_t &old_value, GenericKey *new_key,
                                   const page_id_t &new_value) {  // 根分裂逻辑
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetSize(2);
  SetParentPageId(INVALID_PAGE_ID);
  SetValueAt(0, old_value);
  SetKeyAt(1, new_key);
  SetValueAt(1, new_value);
}

/*
 * Insert new_key & new_value pair right after the pair with its value ==
 * old_value
 * @return:  new size after insertion
 */
int InternalPage::InsertNodeAfter(const page_id_t &old_value, GenericKey *new_key,
                                  const page_id_t &new_value) {  // 在指定值后插入新键值对
  int pos = ValueIndex(old_value);
  ASSERT(pos != -1, "Old value not found in internal page");
  ASSERT(GetSize() <= GetMaxSize(), "Internal page is full");  // 5-13 Debug
  ASSERT(new_key != nullptr, "New key cannot be null");
  for (int i = GetSize(); i > pos + 1; i--) {
    SetKeyAt(i, KeyAt(i - 1));
    SetValueAt(i, ValueAt(i - 1));
  }
  SetKeyAt(pos + 1, new_key);
  SetValueAt(pos + 1, new_value);
  IncreaseSize(1);
  ASSERT(GetSize() <= GetMaxSize() + 1, "Internal page size exceeds maximum size");  // 5-13 Debug
  ASSERT(GetSize() >= 2, "Internal node must have at least 2 pointers");             // 内部节点至少应该有2个指针
  return GetSize();
}

/*****************************************************************************
 * SPLIT
 *****************************************************************************/
/*
 * Remove half of key & value pairs from this page to "recipient" page
 * buffer_pool_manager 是干嘛的？传给CopyNFrom()用于Fetch数据页
 */
void InternalPage::MoveHalfTo(InternalPage *recipient,
                              BufferPoolManager *buffer_pool_manager) {  // 移动上半键值到接受节点
  int max_size = GetMaxSize();
  int move_size = (max_size + 1) - (max_size + 1) / 2;
  void *src = PairPtrAt((max_size + 1) / 2);
  recipient->CopyNFrom(src, move_size, buffer_pool_manager);
  SetSize((max_size + 1) / 2);
}

/* Copy entries into me, starting from {items} and copy {size} entries.
 * Since it is an internal page, for all entries (pages) moved, their parents page now changes to me.
 * So I need to 'adopt' them by changing their parent page id, which needs to be persisted with BufferPoolManger
 *
 */
void InternalPage::CopyNFrom(void *src, int size,
                             BufferPoolManager *buffer_pool_manager) {  // 拷贝size个键值对到当前节点末尾
  void *dest = PairPtrAt(GetSize());
  PairCopy(dest, src, size);
  IncreaseSize(size);
  for (int i = 0; i < size; i++) {
    page_id_t child_page_id = *(page_id_t *)((char *)src + i * pair_size + val_off);
    auto *child_page = reinterpret_cast<BPlusTreePage *>(buffer_pool_manager->FetchPage(child_page_id));
    if (child_page != nullptr) {
      child_page->SetParentPageId(GetPageId());
      buffer_pool_manager->UnpinPage(child_page_id, true);
    }
  }
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Remove the key & value pair in internal page according to input index(a.k.a
 * array offset)
 * NOTE: store key&value pair continuously after deletion
 */
void InternalPage::Remove(int index) {  // 以覆盖方式删除指定索引的键值对
  for (int i = index; i < GetSize() - 1; i++) {
    SetKeyAt(i, KeyAt(i + 1));
    SetValueAt(i, ValueAt(i + 1));
  }
  IncreaseSize(-1);
}

/*
 * Remove the only key & value pair in internal page and return the value
 * NOTE: only call this method within AdjustRoot()(in b_plus_tree.cpp)
 */
page_id_t InternalPage::RemoveAndReturnOnlyChild() {  // 返回单根的子节点
  ASSERT(GetSize() == 1, "Internal page must have exactly one child to remove");
  page_id_t child_page_id = ValueAt(0);
  SetSize(0);
  return child_page_id;
}

/*****************************************************************************
 * MERGE
 *****************************************************************************/
/*
 * Remove all of key & value pairs from this page to "recipient" page.
 * The middle_key is the separation key you should get from the parent. You need
 * to make sure the middle key is added to the recipient to maintain the invariant.
 * You also need to use BufferPoolManager to persist changes to the parent page id for those
 * pages that are moved to the recipient
 */
void InternalPage::MoveAllTo(InternalPage *recipient, GenericKey *middle_key, BufferPoolManager *buffer_pool_manager) {
  int old_recipient_size = recipient->GetSize();
  // 将所有键值对移动到接收节点末尾
  void *src = PairPtrAt(0);
  recipient->CopyNFrom(src, GetSize(), buffer_pool_manager);
  recipient->SetKeyAt(old_recipient_size, middle_key);
  SetSize(0);
}

/*****************************************************************************
 * REDISTRIBUTE
 *****************************************************************************/
/*
 * Remove the first key & value pair from this page to tail of "recipient" page.
 *
 * The middle_key is the separation key you should get from the parent. You need
 * to make sure the middle key is added to the recipient to maintain the ivariant.
 * You also need to use BufferPoolManager to persist changes to the parent page id for those
 * pages that are moved to the recipient
 */
void InternalPage::MoveFirstToEndOf(InternalPage *recipient, GenericKey *middle_key,
                                    BufferPoolManager *buffer_pool_manager) {  // 当前节点借给右兄弟
  page_id_t first_value = ValueAt(0);
  recipient->CopyLastFrom(middle_key, first_value, buffer_pool_manager);
  for (int i = 0; i < GetSize() - 1; i++) {
    SetKeyAt(i, KeyAt(i + 1));
    SetValueAt(i, ValueAt(i + 1));
  }
  IncreaseSize(-1);
}

/* Append an entry at the end.
 * Since it is an internal page, the moved entry(page)'s parent needs to be updated.
 * So I need to 'adopt' it by changing its parent page id, which needs to be persisted with BufferPoolManger
 */
void InternalPage::CopyLastFrom(GenericKey *key, const page_id_t value,
                                BufferPoolManager *buffer_pool_manager) {  // 末尾追加键值对
  int size = GetSize();
  SetKeyAt(size, key);
  SetValueAt(size, value);
  Page *child_page = buffer_pool_manager->FetchPage(value);
  ASSERT(child_page != nullptr, "Failed to fetch child page");
  BPlusTreePage *child_node = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
  child_node->SetParentPageId(GetPageId());
  ;  // 更新父节点归属
  IncreaseSize(1);
  buffer_pool_manager->UnpinPage(value, true);  // 标记为dirty
}

/*
 * Remove the last key & value pair from this page to head of "recipient" page.
 * You need to handle the original dummy key properly, e.g. updating recipient’s array to position the middle_key at the
 * right place.
 * You also need to use BufferPoolManager to persist changes to the parent page id for those pages that are
 * moved to the recipient
 */
void InternalPage::MoveLastToFrontOf(InternalPage *recipient, GenericKey *middle_key,
                                     BufferPoolManager *buffer_pool_manager) {  // 当前节点借给左兄弟
  int last_index = GetSize() - 1;
  page_id_t last_value = ValueAt(last_index);
  recipient->CopyFirstFrom(last_value, buffer_pool_manager);
  recipient->SetKeyAt(1, middle_key);
  IncreaseSize(-1);
}

/* Append an entry at the beginning.
 * Since it is an internal page, the moved entry(page)'s parent needs to be updated.
 * So I need to 'adopt' it by changing its parent page id, which needs to be persisted with BufferPoolManger
 */
void InternalPage::CopyFirstFrom(const page_id_t value, BufferPoolManager *buffer_pool_manager) {  // 在开头插入键值对
  for (int i = GetSize(); i > 0; i--) {
    SetKeyAt(i, KeyAt(i - 1));
    SetValueAt(i, ValueAt(i - 1));
  }
  SetValueAt(0, value);
  Page *child_page = buffer_pool_manager->FetchPage(value);
  ASSERT(child_page != nullptr, "Failed to fetch child page");
  BPlusTreePage *child_node = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
  child_node->SetParentPageId(GetPageId());  // 更新父节点归属
  IncreaseSize(1);
  buffer_pool_manager->UnpinPage(value, true);  // 标记为dirty
}
