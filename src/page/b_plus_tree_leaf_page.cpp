#include "page/b_plus_tree_leaf_page.h"

#include <algorithm>

#include "index/generic_key.h"

#define pairs_off (data_)
#define pair_size (GetKeySize() + sizeof(RowId))
#define key_off 0
#define val_off GetKeySize()
/*****************************************************************************
 * HELPER METHODS AND UTILITIES
 *****************************************************************************/

/**
 * TODO: Student Implement
 */
/**
 * Init method after creating a new leaf page
 * Including set page type, set current size to zero, set page id/parent id, set
 * next page id and set max size
 */
void LeafPage::Init(page_id_t page_id, page_id_t parent_id, int key_size, int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetSize(0);
  SetMaxSize(max_size);
  SetPageId(page_id);
  SetParentPageId(parent_id);
  SetNextPageId(INVALID_PAGE_ID);
  SetKeySize(key_size);
}

/**
 * Helper methods to set/get next page id
 */
page_id_t LeafPage::GetNextPageId() const { return next_page_id_; }

void LeafPage::SetNextPageId(page_id_t next_page_id) {
  next_page_id_ = next_page_id;
  if (next_page_id == 0) {
    LOG(INFO) << "Fatal error";
  }
}

/**
 * TODO: Student Implement
 */
/**
 * Helper method to find the first index i so that pairs_[i].first >= key
 * NOTE: This method is only used when generating index iterator
 */
int LeafPage::KeyIndex(const GenericKey *key, const KeyManager &KM) {  // 找到第一个大于等于key的索引
  ASSERT(GetSize() >= 0, "Leaf page size should be non-negative");
  if (GetSize() == 0) {
    return 0;
  }
  int left = 0;
  int right = GetSize();
  while (left < right) {
    int mid = left + (right - left) / 2;
    if (KM.CompareKeys(KeyAt(mid), key) >= 0) {
      right = mid;
    } else {
      left = mid + 1;
    }
  }
  return left;
}

/*
 * Helper method to find and return the key associated with input "index"(a.k.a
 * array offset)
 */
GenericKey *LeafPage::KeyAt(int index) {
  return reinterpret_cast<GenericKey *>(pairs_off + index * pair_size + key_off);
}

void LeafPage::SetKeyAt(int index, GenericKey *key) {
  memcpy(pairs_off + index * pair_size + key_off, key, GetKeySize());
}

RowId LeafPage::ValueAt(int index) const {
  return *reinterpret_cast<const RowId *>(pairs_off + index * pair_size + val_off);
}

void LeafPage::SetValueAt(int index, RowId value) {
  *reinterpret_cast<RowId *>(pairs_off + index * pair_size + val_off) = value;
}

void *LeafPage::PairPtrAt(int index) { return KeyAt(index); }

void LeafPage::PairCopy(void *dest, void *src, int pair_num) {
  memcpy(dest, src, pair_num * (GetKeySize() + sizeof(RowId)));
}
/*
 * Helper method to find and return the key & value pair associated with input
 * "index"(a.k.a. array offset)
 */
std::pair<GenericKey *, RowId> LeafPage::GetItem(int index) { return {KeyAt(index), ValueAt(index)}; }

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert key & value pair into leaf page ordered by key
 * @return page size after insertion
 * 暂时不进行重复键检测-Debug 5.06
 */
int LeafPage::Insert(GenericKey *key, const RowId &value, const KeyManager &KM) {  // 将键值对插入叶子
  ASSERT(GetSize() < GetMaxSize() + 1, "Leaf page size should be less than max size + 1");
  int insert_index = KeyIndex(key, KM);  // 找到插入位置
  IncreaseSize(1);
  for (int i = GetSize() - 1; i > insert_index; i--) {
    SetKeyAt(i, KeyAt(i - 1));
    SetValueAt(i, ValueAt(i - 1));
  }
  SetKeyAt(insert_index, key);
  SetValueAt(insert_index, value);
  return GetSize();
}

/*****************************************************************************
 * SPLIT
 *****************************************************************************/
/*
 * Remove half of key & value pairs from this page to "recipient" page
 */
void LeafPage::MoveHalfTo(LeafPage *recipient) {  // 将上半元素移动到接收节点
  ASSERT(GetSize() == GetMaxSize() + 1, "Leaf page size should be equal to max size + 1 when splitting");
  int move_size = GetSize() / 2;
  int remain_size = GetSize() - move_size;
  for (int i = 0; i < move_size; i++) {
    recipient->SetKeyAt(i, KeyAt(remain_size + i));
    recipient->SetValueAt(i, ValueAt(remain_size + i));
  }
  recipient->SetNextPageId(GetNextPageId());
  SetNextPageId(recipient->GetPageId());
  SetSize(remain_size);
  recipient->SetSize(move_size);
}

/*
 * Copy starting from items, and copy {size} number of elements into me.
 */
void LeafPage::CopyNFrom(void *src, int size) {
  ASSERT(size <= GetMaxSize(), "Copy size should not exceed max size");
  PairCopy(PairPtrAt(0), src, size);
  SetSize(size);
}

/*****************************************************************************
 * LOOKUP
 *****************************************************************************/
/*
 * For the given key, check to see whether it exists in the leaf page. If it
 * does, then store its corresponding value in input "value" and return true.
 * If the key does not exist, then return false
 */
bool LeafPage::Lookup(const GenericKey *key, RowId &value, const KeyManager &KM) {  // 查找key是否存在，是则返回rid
  int index = KeyIndex(key, KM);
  if (index < GetSize() && KM.CompareKeys(KeyAt(index), key) == 0) {
    value = ValueAt(index);
    return true;
  }
  return false;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * First look through leaf page to see whether delete key exist or not. If
 * existed, perform deletion, otherwise return immediately.
 * NOTE: store key&value pair continuously after deletion
 * @return  page size after deletion
 */
int LeafPage::RemoveAndDeleteRecord(const GenericKey *key, const KeyManager &KM) {  // 以覆盖形式删除键值对
  int index = KeyIndex(key, KM);
  if (index >= GetSize() || KM.CompareKeys(KeyAt(index), key) != 0) {
    return GetSize();
  }
  for (int i = index; i < GetSize() - 1; i++) {
    SetKeyAt(i, KeyAt(i + 1));
    SetValueAt(i, ValueAt(i + 1));
  }
  IncreaseSize(-1);
  return GetSize();
}

/*****************************************************************************
 * MERGE
 *****************************************************************************/
/*
 * Remove all key & value pairs from this page to "recipient" page. Don't forget
 * to update the next_page id in the sibling page
 */
void LeafPage::MoveAllTo(LeafPage *recipient) {  // 将所有元素移动到接收节点末尾
  int size = GetSize();
  for (int i = 0; i < size; i++) {
    recipient->CopyLastFrom(KeyAt(i), ValueAt(i));
  }
  recipient->SetNextPageId(GetNextPageId());
  SetSize(0);
}

/*****************************************************************************
 * REDISTRIBUTE
 *****************************************************************************/
/*
 * Remove the first key & value pair from this page to "recipient" page.
 *
 */
void LeafPage::MoveFirstToEndOf(LeafPage *recipient) {  // 把键值对借给右兄弟
  ASSERT(GetSize() > 0, "Leaf page should not be empty when moving first element");
  GenericKey *first_key = KeyAt(0);
  RowId first_value = ValueAt(0);
  recipient->CopyLastFrom(first_key, first_value);
  for (int i = 0; i < GetSize() - 1; i++) {
    SetKeyAt(i, KeyAt(i + 1));
    SetValueAt(i, ValueAt(i + 1));
  }
  IncreaseSize(-1);
}

/*
 * Copy the item into the end of my item list. (Append item to my array)
 */
void LeafPage::CopyLastFrom(GenericKey *key, const RowId value) {  // 在末尾追加键值对
  ASSERT(GetSize() < GetMaxSize(), "Leaf page size should be less than max size when copying last");
  int size = GetSize();
  SetKeyAt(size, key);
  SetValueAt(size, value);
  IncreaseSize(1);
}

/*
 * Remove the last key & value pair from this page to "recipient" page.
 */
void LeafPage::MoveLastToFrontOf(LeafPage *recipient) {// 把键值对借给左兄弟
  ASSERT(GetSize() > 0, "Leaf page should not be empty when moving last element");
  int last_index = GetSize() - 1;
  GenericKey *last_key = KeyAt(last_index);
  RowId last_value = ValueAt(last_index);
  recipient->CopyFirstFrom(last_key, last_value);
  IncreaseSize(-1);
}

/*
 * Insert item at the front of my items. Move items accordingly.
 *
 */
void LeafPage::CopyFirstFrom(GenericKey *key, const RowId value) {// 在开头插入键值对
  ASSERT(GetSize() < GetMaxSize(), "Leaf page size should be less than max size when copying first");
  for (int i = GetSize(); i > 0; i--) {
    SetKeyAt(i, KeyAt(i - 1));
    SetValueAt(i, ValueAt(i - 1));
  }
  SetKeyAt(0, key);
  SetValueAt(0, value);
  IncreaseSize(1);
}