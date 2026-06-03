#include "index/b_plus_tree.h"

#include <string>

#include "glog/logging.h"
#include "index/basic_comparator.h"
#include "index/generic_key.h"
#include "page/index_roots_page.h"

/**
 * TODO: Student Implement
 */
BPlusTree::BPlusTree(index_id_t index_id, BufferPoolManager *buffer_pool_manager, const KeyManager &KM,
                     int leaf_max_size, int internal_max_size)
    : index_id_(index_id),
      buffer_pool_manager_(buffer_pool_manager),
      processor_(KM),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size) {
  // 预留一个位置，让逻辑满比物理满提早触发
  if (leaf_max_size_ <= 0) {
    leaf_max_size_ = (PAGE_SIZE - LEAF_PAGE_HEADER_SIZE) / (processor_.GetKeySize() + sizeof(RowId)) - 1;
  }
  if (internal_max_size_ <= 0) {
    internal_max_size_ = (PAGE_SIZE - INTERNAL_PAGE_HEADER_SIZE) / (processor_.GetKeySize() + sizeof(page_id_t)) - 1;
  }
  Page *root_page = buffer_pool_manager_->FetchPage(INDEX_ROOTS_PAGE_ID);
  ASSERT(root_page != nullptr, "Failed to fetch index roots page");
  IndexRootsPage *root_page_header =
      reinterpret_cast<IndexRootsPage *>(root_page->GetData());  // 重启后找回索引对应的根页面
  page_id_t root_id;
  if (root_page_header->GetRootId(index_id_, &root_id)) {
    root_page_id_ = root_id;
  }
  buffer_pool_manager_->UnpinPage(INDEX_ROOTS_PAGE_ID, false);
}

void BPlusTree::Destroy(page_id_t current_page_id) {  // 递归删除整棵树页面
  // 如果没有指定当前页面ID，使用根页面ID
  if (current_page_id == INVALID_PAGE_ID && root_page_id_ != INVALID_PAGE_ID) {
    current_page_id = root_page_id_;
  }
  if (current_page_id == INVALID_PAGE_ID) {
    return;
  }
  Page *page = buffer_pool_manager_->FetchPage(current_page_id);
  ASSERT(page != nullptr, "Failed to fetch page");
  BPlusTreePage *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  if (tree_page->IsLeafPage()) {
    // 叶子页直接解固定+删除
    buffer_pool_manager_->UnpinPage(current_page_id, false);
    buffer_pool_manager_->DeletePage(current_page_id);
  } else {
    // 内部页先收集所有子页面ID，再解固定+删除，然后递归处理子页面
    InternalPage *internal_page = reinterpret_cast<InternalPage *>(tree_page);
    int size = internal_page->GetSize();
    std::vector<page_id_t> child_page_ids;
    child_page_ids.reserve(size);
    for (int i = 0; i < size; i++) {
      child_page_ids.push_back(internal_page->ValueAt(i));
    }
    buffer_pool_manager_->UnpinPage(current_page_id, false);
    buffer_pool_manager_->DeletePage(current_page_id);
    for (page_id_t child_page_id : child_page_ids) {
      Destroy(child_page_id);
    }
  }
  if (root_page_id_ == current_page_id) {  // 对于根页面还要删除元数据并无效化
    Page *root_page = buffer_pool_manager_->FetchPage(INDEX_ROOTS_PAGE_ID);
    ASSERT(root_page != nullptr, "Failed to fetch index roots page");
    IndexRootsPage *root_page_header = reinterpret_cast<IndexRootsPage *>(root_page->GetData());
    bool delete_success = root_page_header->Delete(index_id_);
    ASSERT(delete_success, "Failed to delete index from roots page");
    buffer_pool_manager_->UnpinPage(INDEX_ROOTS_PAGE_ID, true);
    root_page_id_ = INVALID_PAGE_ID;
  }
}

/*
 * Helper function to decide whether current b+tree is empty
 */
bool BPlusTree::IsEmpty() const { return root_page_id_ == INVALID_PAGE_ID; }

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/*
 * Return the only value that associated with input key
 * This method is used for point query
 * @return : true means key exists
 */
bool BPlusTree::GetValue(const GenericKey *key, std::vector<RowId> &result, Txn *transaction) {  // 点查询
  if (IsEmpty()) {
    return false;
  }
  Page *leaf_page = FindLeafPage(key);  // 找到对应叶子页
  if (leaf_page == nullptr) {
    return false;
  }
  LeafPage *leaf_node = reinterpret_cast<LeafPage *>(leaf_page->GetData());
  RowId value;
  bool found = leaf_node->Lookup(key, value, processor_);
  if (found) {  // 若找到，加入结果中
    result.push_back(value);
  }
  buffer_pool_manager_->UnpinPage(leaf_node->GetPageId(), false);
  return found;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/*
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
bool BPlusTree::Insert(GenericKey *key, const RowId &value, Txn *transaction) {  // 上层的插入接口
  if (IsEmpty()) {                                                               // 空直接创建新树
    StartNewTree(key, value);
    ASSERT(Check(), "B+ tree is not valid");
    return true;
  }
  ASSERT(Check(), "B+ tree is not valid");
  return InsertIntoLeaf(key, value, transaction);  // 否则委托下层插入
}
/*
 * Insert constant key & value pair into an empty tree
 * User needs to first ask for new page from buffer pool manager(NOTICE: throw
 * an "out of memory" exception if returned value is nullptr), then update b+
 * tree's root page id and insert entry directly into leaf page.
 */
void BPlusTree::StartNewTree(GenericKey *key, const RowId &value) {  // 以一个键值对创建新树
  page_id_t new_page_id;
  Page *new_page = buffer_pool_manager_->NewPage(new_page_id);
  ASSERT(new_page != nullptr, "Failed to create new page");
  LeafPage *leaf_page = reinterpret_cast<LeafPage *>(new_page->GetData());
  leaf_page->Init(new_page_id, INVALID_PAGE_ID, processor_.GetKeySize(), leaf_max_size_);
  leaf_page->Insert(key, value, processor_);
  root_page_id_ = new_page_id;
  UpdateRootPageId(1);  // 插入一条新的目录记录
  buffer_pool_manager_->UnpinPage(new_page_id, true);
}

/*
 * Insert constant key & value pair into leaf page
 * User needs to first find the right leaf page as insertion target, then look
 * through leaf page to see whether insert key exist or not. If exist, return
 * immediately, otherwise insert entry. Remember to deal with split if necessary.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
bool BPlusTree::InsertIntoLeaf(GenericKey *key, const RowId &value, Txn *transaction) {  // 向非空树插入
  Page *leaf_page = FindLeafPage(key);
  if (leaf_page == nullptr) {
    return false;
  }
  LeafPage *leaf_node = reinterpret_cast<LeafPage *>(leaf_page->GetData());
  RowId existing_value;
  if (leaf_node->Lookup(key, existing_value, processor_)) {  // 重复键
    buffer_pool_manager_->UnpinPage(leaf_node->GetPageId(), false);
    return false;
  }
  leaf_node->Insert(key, value, processor_);
  // 检查是否需要分裂
  if (leaf_node->GetSize() > leaf_node->GetMaxSize()) {
    LeafPage *new_leaf = Split(leaf_node, transaction);
    ASSERT(new_leaf != nullptr, "Split failed, out of memory");
    GenericKey *middle_key = new_leaf->KeyAt(0);
    InsertIntoParent(leaf_node, middle_key, new_leaf, transaction);  // 将新的分割键插入父节点
    // 5-25 Debug: new_leaf is already unpinned inside InsertIntoParent
  }
  buffer_pool_manager_->UnpinPage(leaf_node->GetPageId(), true);
  return true;
}

/*
 * Split input page and return newly created page.
 * Using template N to represent either internal page or leaf page.
 * User needs to first ask for new page from buffer pool manager(NOTICE: throw
 * an "out of memory" exception if returned value is nullptr), then move half
 * of key & value pairs from input page to newly created page
 */
BPlusTreeInternalPage *BPlusTree::Split(InternalPage *node, Txn *transaction) {  // 内部页重载分裂
  page_id_t new_page_id;
  Page *new_page = buffer_pool_manager_->NewPage(new_page_id);
  ASSERT(new_page != nullptr, "Failed to create new page for internal node split");
  InternalPage *new_node = reinterpret_cast<InternalPage *>(new_page->GetData());
  new_node->Init(new_page_id, node->GetParentPageId(), node->GetKeySize(), internal_max_size_);
  node->MoveHalfTo(new_node, buffer_pool_manager_);
  return new_node;
}

BPlusTreeLeafPage *BPlusTree::Split(LeafPage *node, Txn *transaction) {  // 叶子页重载分裂
  page_id_t new_page_id;
  Page *new_page = buffer_pool_manager_->NewPage(new_page_id);
  ASSERT(new_page != nullptr, "Failed to create new page for leaf node split");
  LeafPage *new_node = reinterpret_cast<LeafPage *>(new_page->GetData());
  new_node->Init(new_page_id, node->GetParentPageId(), node->GetKeySize(), leaf_max_size_);
  node->MoveHalfTo(new_node);
  return new_node;
}

/*
 * Insert key & value pair into internal page after split
 * @param   old_node      input page from split() method
 * @param   key
 * @param   new_node      returned page from split() method
 * User needs to first find the parent page of old_node, parent node must be
 * adjusted to take info of new_node into account. Remember to deal with split
 * recursively if necessary.
 */
void BPlusTree::InsertIntoParent(BPlusTreePage *old_node, GenericKey *key, BPlusTreePage *new_node,
                                 Txn *transaction) {  // 处理递归向上分裂
  if (old_node->IsRootPage()) {                       // 如果old_node是根节点，需要创建新的根节点
    page_id_t new_root_page_id;
    Page *new_root_page = buffer_pool_manager_->NewPage(new_root_page_id);
    ASSERT(new_root_page != nullptr, "Failed to create new root page");
    InternalPage *new_root = reinterpret_cast<InternalPage *>(new_root_page->GetData());
    new_root->Init(new_root_page_id, INVALID_PAGE_ID, old_node->GetKeySize(), internal_max_size_);
    new_root->PopulateNewRoot(old_node->GetPageId(), key, new_node->GetPageId());
    root_page_id_ = new_root_page_id;
    UpdateRootPageId(0);
    // 设置old_node和new_node的父页面为新根
    old_node->SetParentPageId(new_root_page_id);
    new_node->SetParentPageId(new_root_page_id);
    // 此处old_node的Unpin由调用者负责
    buffer_pool_manager_->UnpinPage(new_root_page_id, true);
    buffer_pool_manager_->UnpinPage(new_node->GetPageId(), true);
  } else {
    page_id_t parent_page_id = old_node->GetParentPageId();
    Page *parent_page = buffer_pool_manager_->FetchPage(parent_page_id);
    ASSERT(parent_page != nullptr, "Failed to fetch parent page");
    InternalPage *parent_node = reinterpret_cast<InternalPage *>(parent_page->GetData());
    new_node->SetParentPageId(parent_page_id);
    buffer_pool_manager_->UnpinPage(new_node->GetPageId(), true);
    parent_node->InsertNodeAfter(old_node->GetPageId(), key, new_node->GetPageId());
    // 检查父节点插入后是否需要分裂
    if (parent_node->GetSize() > parent_node->GetMaxSize()) {
      InternalPage *new_parent = Split(parent_node, transaction);
      ASSERT(new_parent != nullptr, "Split failed, out of memory");
      GenericKey *middle_key = new_parent->KeyAt(0);
      InsertIntoParent(parent_node, middle_key, new_parent, transaction);
    }
    buffer_pool_manager_->UnpinPage(parent_page_id, true);
  }
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
void BPlusTree::Remove(const GenericKey *key, Txn *transaction) {
  if (IsEmpty()) return;
  Page *leaf_page = FindLeafPage(key);
  if (leaf_page == nullptr) return;
  LeafPage *leaf_node = reinterpret_cast<LeafPage *>(leaf_page->GetData());
  int old_size = leaf_node->GetSize();
  int key_index = leaf_node->KeyIndex(key, processor_);
  if (key_index == 0 && !leaf_node->IsRootPage() && old_size >= 2 &&
      processor_.CompareKeys(leaf_node->KeyAt(0), key) == 0) {  // 更新父节点中的分隔键
    page_id_t parent_id = leaf_node->GetParentPageId();
    Page *parent_page = buffer_pool_manager_->FetchPage(parent_id);
    if (parent_page != nullptr) {
      InternalPage *parent_node = reinterpret_cast<InternalPage *>(parent_page->GetData());
      int child_index = parent_node->ValueIndex(leaf_node->GetPageId());
      if (child_index != -1 && child_index > 0) {
        parent_node->SetKeyAt(child_index, leaf_node->KeyAt(1));
      }
      buffer_pool_manager_->UnpinPage(parent_id, true);
    }
  }
  // 执行删除
  leaf_node->RemoveAndDeleteRecord(key, processor_);
  int new_size = leaf_node->GetSize();
  if (old_size == new_size) {  // 未删除任何东西
    buffer_pool_manager_->UnpinPage(leaf_node->GetPageId(), false);
    return;
  }
  // 下溢处理
  if (new_size < leaf_node->GetMinSize()) {
    if (leaf_node->IsRootPage()) {
      bool root_deleted = AdjustRoot(leaf_node);
      if (!root_deleted) {
        buffer_pool_manager_->UnpinPage(leaf_node->GetPageId(), true);
      }
    } else {
      CoalesceOrRedistribute(leaf_node, transaction);
    }
  } else {
    buffer_pool_manager_->UnpinPage(leaf_node->GetPageId(), true);
  }
}

/* todo
 * User needs to first find the sibling of input page. If sibling's size + input
 * page's size > page's max size, then redistribute. Otherwise, merge.
 * Using template N to represent either internal page or leaf page.
 * @return: true means target leaf page should be deleted, false means no
 * deletion happens
 */
template <typename N>
bool BPlusTree::CoalesceOrRedistribute(N *&node, Txn *transaction) {
  if (node->IsRootPage()) {
    return false;
  }
  page_id_t parent_page_id = node->GetParentPageId();
  Page *parent_page = buffer_pool_manager_->FetchPage(parent_page_id);
  ASSERT(parent_page != nullptr, "Failed to fetch parent page");
  InternalPage *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());
  int node_index = parent->ValueIndex(node->GetPageId());
  int sibling_index;
  bool is_right_sibling;
  if (node_index > 0) {
    // 优先选择左兄弟
    sibling_index = node_index - 1;
    is_right_sibling = false;
  } else {
    sibling_index = node_index + 1;
    is_right_sibling = true;
  }
  page_id_t sibling_page_id = parent->ValueAt(sibling_index);
  Page *sibling_page = buffer_pool_manager_->FetchPage(sibling_page_id);
  ASSERT(sibling_page != nullptr, "Failed to fetch sibling page");
  N *sibling = reinterpret_cast<N *>(sibling_page->GetData());
  int total_size = node->GetSize() + sibling->GetSize();
  if (total_size > node->GetMaxSize()) {  // 从兄弟借调一个
    buffer_pool_manager_->UnpinPage(parent_page_id, false);
    if (is_right_sibling) {
      Redistribute(sibling, node, 0);
    } else {
      Redistribute(sibling, node, sibling_index + 1);
    }
    buffer_pool_manager_->UnpinPage(sibling_page_id, true);
    buffer_pool_manager_->UnpinPage(node->GetPageId(), true);
    return false;
  } else {  // 跟兄弟合并
    bool parent_underflow;
    if (is_right_sibling) {
      parent_underflow = Coalesce(node, sibling, parent, 0, transaction);
    } else {
      parent_underflow = Coalesce(sibling, node, parent, 1, transaction);
    }
    if (!parent_underflow) {
      buffer_pool_manager_->UnpinPage(parent_page_id, true);
      return true;
    } else {
      if (parent->IsRootPage()) {
        AdjustRoot(parent);
      } else {
        CoalesceOrRedistribute(parent, transaction);
      }
      return true;
    }
  }
}

/*
 * Move all the key & value pairs from one page to its sibling page, and notify
 * buffer pool manager to delete this page. Parent page must be adjusted to
 * take info of deletion into account. Remember to deal with coalesce or
 * redistribute recursively if necessary.
 * Using template N to represent either internal page or leaf page.
 * @param   neighbor_node      sibling page of input "node"
 * @param   node               input from method coalesceOrRedistribute()
 * @param   parent             parent page of input "node"
 * @return  true means parent node should be deleted, false means no deletion happened
 */
bool BPlusTree::Coalesce(LeafPage *&neighbor_node, LeafPage *&node, InternalPage *&parent, int index,
                         Txn *transaction) {
  // [5-20 Debug] neighbor_node恒为左页，node恒为右页
  // CoalesceOrRedistribute调用本函数时，若is_right_sibling==true则传Coalesce(node, sibling, parent, 0)，
  // node在parent中index=0(左)，sibling在index=1(右)；若is_right_sibling==false则传
  // Coalesce(sibling, node, parent, 1)，sibling在左，node在右。因此无论index值，neighbor_node始终
  // 为左侧页、node始终为右侧页。此前index==0分支误将二者颠倒，导致：(1)左页被DeletePage但前驱叶子
  // 的NextPageId仍指向它，链表断裂；(2)后续遍历或迭代器沿链表走到已删除页。
  LeafPage *left_page, *right_page;
  left_page = neighbor_node;
  right_page = node;
  // 将右兄弟的所有键值对移动到左兄弟
  right_page->MoveAllTo(left_page);
  int right_index = parent->ValueIndex(right_page->GetPageId());
  parent->Remove(right_index);
  // 删除右兄弟页面并unpin左兄弟页面
  buffer_pool_manager_->UnpinPage(right_page->GetPageId(), true);
  buffer_pool_manager_->DeletePage(right_page->GetPageId());
  buffer_pool_manager_->UnpinPage(left_page->GetPageId(), true);
  node = left_page;
  return parent->GetSize() < parent->GetMinSize();
}

bool BPlusTree::Coalesce(InternalPage *&neighbor_node, InternalPage *&node, InternalPage *&parent, int index,
                         Txn *transaction) {
  // [5-20 Debug] 错误同上
  InternalPage *left_page, *right_page;
  left_page = neighbor_node;
  right_page = node;
  int right_index = parent->ValueIndex(right_page->GetPageId());
  GenericKey *middle_key = parent->KeyAt(right_index);
  // 将右兄弟的所有键值对移动到左兄弟
  right_page->MoveAllTo(left_page, middle_key, buffer_pool_manager_);
  parent->Remove(right_index);
  // 删除右兄弟页面并unpin左兄弟页面
  buffer_pool_manager_->UnpinPage(right_page->GetPageId(), true);
  buffer_pool_manager_->DeletePage(right_page->GetPageId());
  buffer_pool_manager_->UnpinPage(left_page->GetPageId(), true);
  node = left_page;
  return parent->GetSize() < parent->GetMinSize();
}

/*
 * Redistribute key & value pairs from one page to its sibling page. If index ==
 * 0, move sibling page's first key & value pair into end of input "node",
 * otherwise move sibling page's last key & value pair into head of input
 * "node".
 * Using template N to represent either internal page or leaf page.
 * @param   neighbor_node      sibling page of input "node"
 * @param   node               input from method coalesceOrRedistribute()
 */
void BPlusTree::Redistribute(LeafPage *neighbor_node, LeafPage *node, int index) {
  page_id_t parent_page_id = node->GetParentPageId();
  Page *parent_page = buffer_pool_manager_->FetchPage(parent_page_id);
  ASSERT(parent_page != nullptr, "Failed to fetch parent page");
  InternalPage *parent_node = reinterpret_cast<InternalPage *>(parent_page->GetData());
  if (index == 0) {  // 本身是最左侧节点，向右兄弟借后要更新父节点索引
    neighbor_node->MoveFirstToEndOf(node);
    int neighbor_index = parent_node->ValueIndex(neighbor_node->GetPageId());
    if (neighbor_index > 0) {
      parent_node->SetKeyAt(neighbor_index, neighbor_node->KeyAt(0));
    }
  } else {  // 否则向左兄弟借来末尾节点，更新父节点索引
    neighbor_node->MoveLastToFrontOf(node);
    int node_index = parent_node->ValueIndex(node->GetPageId());
    if (node_index > 0) {
      parent_node->SetKeyAt(node_index, node->KeyAt(0));
    }
  }
  buffer_pool_manager_->UnpinPage(parent_page_id, true);
}

void BPlusTree::Redistribute(InternalPage *neighbor_node, InternalPage *node, int index) {
  page_id_t parent_page_id = node->GetParentPageId();
  Page *parent_page = buffer_pool_manager_->FetchPage(parent_page_id);
  ASSERT(parent_page != nullptr, "Failed to fetch parent page");
  InternalPage *parent_node = reinterpret_cast<InternalPage *>(parent_page->GetData());
  GenericKey *middle_key;
  if (index == 0) {  // 本身是最左侧节点，借来父节点的分割键后将右兄弟最小节点提为父节点分割键
    int node_index = parent_node->ValueIndex(node->GetPageId());
    middle_key = parent_node->KeyAt(node_index + 1);
    neighbor_node->MoveFirstToEndOf(node, middle_key, buffer_pool_manager_);
    parent_node->SetKeyAt(node_index + 1, neighbor_node->KeyAt(1));
  } else {  // 同理操作
    int neighbor_index = parent_node->ValueIndex(neighbor_node->GetPageId());
    middle_key = parent_node->KeyAt(neighbor_index + 1);
    neighbor_node->MoveLastToFrontOf(node, middle_key, buffer_pool_manager_);
    parent_node->SetKeyAt(neighbor_index + 1, node->KeyAt(1));
  }
  buffer_pool_manager_->UnpinPage(parent_page_id, true);
}

/*
 * Update root page if necessary
 * NOTE: size of root page can be less than min size and this method is only
 * called within coalesceOrRedistribute() method
 * case 1: when you delete the last element in root page, but root page still
 * has one last child
 * case 2: when you delete the last element in whole b+ tree
 * @return : true means root page should be deleted, false means no deletion
 * happened
 */
bool BPlusTree::AdjustRoot(BPlusTreePage *old_root_node) {
  // 根节点只剩下唯一孩子节点
  if (!old_root_node->IsLeafPage() && old_root_node->GetSize() == 1) {
    InternalPage *old_root = reinterpret_cast<InternalPage *>(old_root_node);
    page_id_t new_root_page_id = old_root->RemoveAndReturnOnlyChild();
    buffer_pool_manager_->UnpinPage(old_root->GetPageId(), false);
    buffer_pool_manager_->DeletePage(old_root->GetPageId());
    root_page_id_ = new_root_page_id;
    UpdateRootPageId(0);
    Page *new_root_page = buffer_pool_manager_->FetchPage(new_root_page_id);
    ASSERT(new_root_page != nullptr, "Failed to fetch new root page");
    BPlusTreePage *new_root_node = reinterpret_cast<BPlusTreePage *>(new_root_page->GetData());
    new_root_node->SetParentPageId(INVALID_PAGE_ID);
    buffer_pool_manager_->UnpinPage(new_root_page_id, true);
  }
  if (old_root_node->IsLeafPage() && old_root_node->GetSize() == 0) {  // 只剩最后一个键值对
    buffer_pool_manager_->UnpinPage(old_root_node->GetPageId(), false);
    buffer_pool_manager_->DeletePage(old_root_node->GetPageId());
    root_page_id_ = INVALID_PAGE_ID;
    UpdateRootPageId(0);
    return true;
  }
  return false;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the left most leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
IndexIterator BPlusTree::Begin() {
  if (IsEmpty()) {
    return IndexIterator();
  }
  Page *leftmost_leaf_page = FindLeafPage(nullptr, INVALID_PAGE_ID, true);  // 沿着最左侧走到底
  if (leftmost_leaf_page == nullptr) {
    return IndexIterator();
  }
  LeafPage *leftmost_leaf = reinterpret_cast<LeafPage *>(leftmost_leaf_page->GetData());
  page_id_t page_id = leftmost_leaf->GetPageId();
  // 先 Unpin，让 IndexIterator 自己管理页面的生命周期
  buffer_pool_manager_->UnpinPage(page_id, false);
  return IndexIterator(page_id, buffer_pool_manager_, 0);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
IndexIterator BPlusTree::Begin(const GenericKey *key) {  // 指定获取起始键的迭代器
  if (IsEmpty()) {
    return IndexIterator();
  }
  Page *leaf_page = FindLeafPage(key);  // 找到包含key的叶节点
  if (leaf_page == nullptr) {
    return IndexIterator();
  }
  LeafPage *leaf_node = reinterpret_cast<LeafPage *>(leaf_page->GetData());
  // 找到大于等于 key 的第一个位置
  int index = leaf_node->KeyIndex(key, processor_);
  page_id_t page_id = leaf_node->GetPageId();
  buffer_pool_manager_->UnpinPage(page_id, false);
  return IndexIterator(page_id, buffer_pool_manager_, index);
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
IndexIterator BPlusTree::End() { return IndexIterator(); }

/*****************************************************************************
 * UTILITIES AND DEBUG
 *****************************************************************************/
/*
 * Find leaf page containing particular key, if leftMost flag == true, find
 * the left most leaf page
 * Note: the leaf page is pinned, you need to unpin it after use.
 */
Page *BPlusTree::FindLeafPage(const GenericKey *key, page_id_t page_id, bool leftMost) {
  if (page_id == INVALID_PAGE_ID) {  // 如果没有指定起始页面ID，使用根页面ID
    if (root_page_id_ == INVALID_PAGE_ID) {
      return nullptr;
    }
    page_id = root_page_id_;
  }
  Page *page = buffer_pool_manager_->FetchPage(page_id);
  if (page == nullptr) {
    return nullptr;
  }
  BPlusTreePage *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
  if (tree_page->IsLeafPage()) {  // 递归中止
    return page;
  }
  // 如果是内部页面，需要继续向下查找
  InternalPage *internal_page = reinterpret_cast<InternalPage *>(tree_page);
  page_id_t next_page_id;
  if (leftMost) {
    // 查找最左边的叶子页面，总是选择第一个子指针
    next_page_id = internal_page->ValueAt(0);
  } else {
    // 根据key查找对应的子页面
    next_page_id = internal_page->Lookup(key, processor_);
  }
  buffer_pool_manager_->UnpinPage(page_id, false);
  return FindLeafPage(key, next_page_id, leftMost);
}

/*
 * Update/Insert root page id in header page(where page_id = INDEX_ROOTS_PAGE_ID,
 * header_page isdefined under include/page/header_page.h)
 * Call this method everytime root page id is changed.
 * @parameter: insert_record      default value is false. When set to true,
 * insert a record <index_name, current_page_id> into header page instead of
 * updating it.
 */
void BPlusTree::UpdateRootPageId(int insert_record) {  // 将根页面id持久化
  Page *root_page = buffer_pool_manager_->FetchPage(INDEX_ROOTS_PAGE_ID);
  ASSERT(root_page != nullptr, "Failed to fetch index roots page");
  IndexRootsPage *root_page_header = reinterpret_cast<IndexRootsPage *>(root_page->GetData());
  bool success;
  if (insert_record) {
    success = root_page_header->Insert(index_id_, root_page_id_);
  } else {
    success = root_page_header->Update(index_id_, root_page_id_);
  }
  ASSERT(success, "Failed to update root page id in index roots page");
  buffer_pool_manager_->UnpinPage(INDEX_ROOTS_PAGE_ID, true);
}

/**
 * This method is used for debug only, You don't need to modify
 */
void BPlusTree::ToGraph(BPlusTreePage *page, BufferPoolManager *bpm, std::ofstream &out, Schema *schema) const {
  std::string leaf_prefix("LEAF_");
  std::string internal_prefix("INT_");
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<LeafPage *>(page);
    // Print node name
    out << leaf_prefix << leaf->GetPageId();
    // Print node properties
    out << "[shape=plain color=green ";
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">P=" << leaf->GetPageId()
        << ",Parent=" << leaf->GetParentPageId() << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << leaf->GetSize() << "\">"
        << "max_size=" << leaf->GetMaxSize() << ",min_size=" << leaf->GetMinSize() << ",size=" << leaf->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < leaf->GetSize(); i++) {
      Row ans;
      processor_.DeserializeToKey(leaf->KeyAt(i), ans, schema);
      out << "<TD>" << ans.GetField(0)->toString() << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Leaf node link if there is a next page
    if (leaf->GetNextPageId() != INVALID_PAGE_ID) {
      out << leaf_prefix << leaf->GetPageId() << " -> " << leaf_prefix << leaf->GetNextPageId() << ";\n";
      out << "{rank=same " << leaf_prefix << leaf->GetPageId() << " " << leaf_prefix << leaf->GetNextPageId() << "};\n";
    }

    // Print parent links if there is a parent
    if (leaf->GetParentPageId() != INVALID_PAGE_ID) {
      out << internal_prefix << leaf->GetParentPageId() << ":p" << leaf->GetPageId() << " -> " << leaf_prefix
          << leaf->GetPageId() << ";\n";
    }
  } else {
    auto *inner = reinterpret_cast<InternalPage *>(page);
    // Print node name
    out << internal_prefix << inner->GetPageId();
    // Print node properties
    out << "[shape=plain color=pink ";  // why not?
    // Print data of the node
    out << "label=<<TABLE BORDER=\"0\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\">\n";
    // Print data
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">P=" << inner->GetPageId()
        << ",Parent=" << inner->GetParentPageId() << "</TD></TR>\n";
    out << "<TR><TD COLSPAN=\"" << inner->GetSize() << "\">"
        << "max_size=" << inner->GetMaxSize() << ",min_size=" << inner->GetMinSize() << ",size=" << inner->GetSize()
        << "</TD></TR>\n";
    out << "<TR>";
    for (int i = 0; i < inner->GetSize(); i++) {
      out << "<TD PORT=\"p" << inner->ValueAt(i) << "\">";
      if (i > 0) {
        Row ans;
        processor_.DeserializeToKey(inner->KeyAt(i), ans, schema);
        out << ans.GetField(0)->toString();
      } else {
        out << " ";
      }
      out << "</TD>\n";
    }
    out << "</TR>";
    // Print table end
    out << "</TABLE>>];\n";
    // Print Parent link
    if (inner->GetParentPageId() != INVALID_PAGE_ID) {
      out << internal_prefix << inner->GetParentPageId() << ":p" << inner->GetPageId() << " -> " << internal_prefix
          << inner->GetPageId() << ";\n";
    }
    // Print leaves
    for (int i = 0; i < inner->GetSize(); i++) {
      auto child_page = reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(inner->ValueAt(i))->GetData());
      ToGraph(child_page, bpm, out, schema);
      if (i > 0) {
        auto sibling_page = reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(inner->ValueAt(i - 1))->GetData());
        if (!sibling_page->IsLeafPage() && !child_page->IsLeafPage()) {
          out << "{rank=same " << internal_prefix << sibling_page->GetPageId() << " " << internal_prefix
              << child_page->GetPageId() << "};\n";
        }
        bpm->UnpinPage(sibling_page->GetPageId(), false);
      }
    }
  }
  bpm->UnpinPage(page->GetPageId(), false);
}

/**
 * This function is for debug only, you don't need to modify
 */
void BPlusTree::ToString(BPlusTreePage *page, BufferPoolManager *bpm) const {
  if (page->IsLeafPage()) {
    auto *leaf = reinterpret_cast<LeafPage *>(page);
    std::cout << "Leaf Page: " << leaf->GetPageId() << " parent: " << leaf->GetParentPageId()
              << " next: " << leaf->GetNextPageId() << std::endl;
    for (int i = 0; i < leaf->GetSize(); i++) {
      std::cout << leaf->KeyAt(i) << ",";
    }
    std::cout << std::endl;
    std::cout << std::endl;
  } else {
    auto *internal = reinterpret_cast<InternalPage *>(page);
    std::cout << "Internal Page: " << internal->GetPageId() << " parent: " << internal->GetParentPageId() << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      std::cout << internal->KeyAt(i) << ": " << internal->ValueAt(i) << ",";
    }
    std::cout << std::endl;
    std::cout << std::endl;
    for (int i = 0; i < internal->GetSize(); i++) {
      ToString(reinterpret_cast<BPlusTreePage *>(bpm->FetchPage(internal->ValueAt(i))->GetData()), bpm);
      bpm->UnpinPage(internal->ValueAt(i), false);
    }
  }
}

bool BPlusTree::Check() {
  bool all_unpinned = buffer_pool_manager_->CheckAllUnpinned();
  if (!all_unpinned) {
    LOG(ERROR) << "problem in page unpin" << endl;
  }
  return all_unpinned;
}