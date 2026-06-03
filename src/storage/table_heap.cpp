#include "storage/table_heap.h"

#include "page/table_page.h"

/**
 * TODO: Student Implement
 */
TableHeap::TableHeap(BufferPoolManager *buffer_pool_manager, Schema *schema, Txn *txn, LogManager *log_manager,
                     LockManager *lock_manager)
    : buffer_pool_manager_(buffer_pool_manager),
      first_page_id_(INVALID_PAGE_ID),
      schema_(schema),
      log_manager_(log_manager),
      lock_manager_(lock_manager) {
  page_id_t new_page_id = INVALID_PAGE_ID;
  Page *new_page = buffer_pool_manager_->NewPage(new_page_id);  // 分配新页
  if (new_page == nullptr || new_page_id == INVALID_PAGE_ID) {
    return;
  }
  TablePage *table_page = reinterpret_cast<TablePage *>(new_page);  // 初始化页面
  table_page->WLatch();
  table_page->Init(new_page_id, INVALID_PAGE_ID, log_manager_, txn);
  table_page->WUnlatch();
  // 设置首页标志和首空闲页标志
  first_page_id_ = new_page_id;
  first_free_page_id_ = new_page_id;
  buffer_pool_manager_->UnpinPage(new_page_id, true);  // 解固定并标记为脏页
}

/**
 * TODO: Student Implement
 */
bool TableHeap::InsertTuple(Row &row, Txn *txn) {
  if (row.GetSerializedSize(schema_) >= PAGE_SIZE) {  // 元组大小超过页面大小
    return false;
  }
  // 从空闲页开始遍历页面链表，查找有足够空间的页
  page_id_t current_page_id = first_free_page_id_;
  TablePage *page = nullptr;
  page_id_t last_page_id = INVALID_PAGE_ID;
  while (current_page_id != INVALID_PAGE_ID) {
    last_page_id = current_page_id;
    page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(current_page_id));
    if (page == nullptr) {
      return false;
    }
    // 尝试在当前页面插入
    page->WLatch();
    bool inserted = page->InsertTuple(row, schema_, txn, lock_manager_, log_manager_);
    page->WUnlatch();
    // 如果插入成功，更新空闲页提示并返回
    if (inserted) {
      first_free_page_id_ = current_page_id;
      buffer_pool_manager_->UnpinPage(current_page_id, true);
      return true;
    }
    // 当前页面已满，Unpin 并继续查找下一页
    buffer_pool_manager_->UnpinPage(current_page_id, false);  // 未插入，不置脏标记
    current_page_id = page->GetNextPageId();
  }
  // 所有页面都满了，创建新页
  page_id_t new_page_id = INVALID_PAGE_ID;
  Page *new_page = buffer_pool_manager_->NewPage(new_page_id);
  if (new_page == nullptr || new_page_id == INVALID_PAGE_ID) {
    return false;
  }
  page = reinterpret_cast<TablePage *>(new_page);
  page->WLatch();
  page->Init(new_page_id, last_page_id, log_manager_, txn);
  bool inserted = page->InsertTuple(row, schema_, txn, lock_manager_, log_manager_);
  page->WUnlatch();
  if (!inserted) {  // 如果插入失败，回退
    buffer_pool_manager_->UnpinPage(new_page_id, false);
    buffer_pool_manager_->DeletePage(new_page_id);
    return false;
  }
  // 将新页面放入页面链表中
  if (last_page_id != INVALID_PAGE_ID) {
    // 获取最后一页并更新其 next_page_id
    TablePage *last_page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(last_page_id));
    if (last_page == nullptr) {
      buffer_pool_manager_->UnpinPage(new_page_id, false);
      buffer_pool_manager_->DeletePage(new_page_id);
      return false;
    }
    last_page->WLatch();
    last_page->SetNextPageId(new_page_id);
    last_page->WUnlatch();
    buffer_pool_manager_->UnpinPage(last_page_id, true);
  } else {
    // 空链表，初始化 first_page_id
    first_page_id_ = new_page_id;
  }
  // 更新空闲页提示并 Unpin 新页面
  first_free_page_id_ = new_page_id;
  buffer_pool_manager_->UnpinPage(new_page_id, true);
  return true;
}

bool TableHeap::MarkDelete(const RowId &rid, Txn *txn) {
  // Find the page which contains the tuple.
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  // If the page could not be found, then abort the recovery.
  if (page == nullptr) {
    return false;
  }
  // Otherwise, mark the tuple as deleted.
  page->WLatch();
  page->MarkDelete(rid, txn, lock_manager_, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
  return true;
}

/**
 * TODO: Student Implement
 */
bool TableHeap::UpdateTuple(Row &row, const RowId &rid, Txn *txn) {
  // 根据 rid 找到对应的页面
  page_id_t page_id = rid.GetPageId();
  if (page_id == INVALID_PAGE_ID) {
    return false;
  }
  // 获取包含该元组的页面
  TablePage *page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(page_id));
  if (page == nullptr) {
    return false;
  }
  // 准备一个临时 row 用于存储旧数据
  Row old_row(rid);
  page->WLatch();
  bool updated = page->UpdateTuple(row, &old_row, schema_, txn, lock_manager_, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page_id, true);
  return updated;
}

/**
 * TODO: Student Implement
 */
void TableHeap::ApplyDelete(const RowId &rid, Txn *txn) {
  // 找到包含该元组的页面
  TablePage *page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  if (page == nullptr) {
    return;
  }
  page->WLatch();
  page->ApplyDelete(rid, txn, log_manager_);
  page->WUnlatch();
  // 更新空闲页提示
  first_free_page_id_ = rid.GetPageId();
  buffer_pool_manager_->UnpinPage(rid.GetPageId(), true);
}

void TableHeap::RollbackDelete(const RowId &rid, Txn *txn) {
  // Find the page which contains the tuple.
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(rid.GetPageId()));
  assert(page != nullptr);
  // Rollback to delete.
  page->WLatch();
  page->RollbackDelete(rid, txn, log_manager_);
  page->WUnlatch();
  buffer_pool_manager_->UnpinPage(page->GetTablePageId(), true);
}

/**
 * TODO: Student Implement
 */
bool TableHeap::GetTuple(Row *row, Txn *txn) {
  // rid 合法性检查
  if (row == nullptr || row->GetRowId().Get() == INVALID_ROWID.Get()) {
    return false;
  }
  page_id_t page_id = row->GetRowId().GetPageId();
  if (page_id == INVALID_PAGE_ID) {
    return false;
  }
  // 获取包含该元组的页面
  TablePage *page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(page_id));
  if (page == nullptr) {
    return false;
  }
  page->RLatch();
  bool success = page->GetTuple(row, schema_, txn, lock_manager_);
  page->RUnlatch();
  buffer_pool_manager_->UnpinPage(page_id, false);
  return success;
}

void TableHeap::DeleteTable(page_id_t page_id) {
  if (page_id != INVALID_PAGE_ID) {
    auto temp_table_page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(page_id));  // 删除table_heap
    if (temp_table_page->GetNextPageId() != INVALID_PAGE_ID) DeleteTable(temp_table_page->GetNextPageId());
    buffer_pool_manager_->UnpinPage(page_id, false);
    buffer_pool_manager_->DeletePage(page_id);
  } else {
    DeleteTable(first_page_id_);
  }
}

/**
 * TODO: Student Implement
 */
TableIterator TableHeap::Begin(Txn *txn) {
  // 合法性检查
  if (first_page_id_ == INVALID_PAGE_ID) {
    return TableIterator(this, INVALID_ROWID, txn);
  }
  // 获取第一个页面
  TablePage *first_page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(first_page_id_));
  if (first_page == nullptr) {
    return TableIterator(this, INVALID_ROWID, txn);
  }
  // 获取第一个有效元组的 RID
  RowId first_rid;
  first_page->RLatch();
  bool found = first_page->GetFirstTupleRid(&first_rid);
  first_page->RUnlatch();
  buffer_pool_manager_->UnpinPage(first_page_id_, false);
  // 如果找到有效元组，返回指向它的迭代器；否则返回结束迭代器
  if (found) {
    return TableIterator(this, first_rid, txn);
  } else {
    return TableIterator(this, INVALID_ROWID, txn);
  }
}

/**
 * TODO: Student Implement
 */
TableIterator TableHeap::End() {
  // 返回结束迭代器
  return TableIterator(this, INVALID_ROWID, nullptr);
}
