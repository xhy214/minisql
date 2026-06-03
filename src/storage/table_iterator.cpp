#include "storage/table_iterator.h"

#include "common/macros.h"
#include "storage/table_heap.h"

/**
 * TODO: Student Implement
 */
TableIterator::TableIterator(TableHeap *table_heap, RowId rid, Txn *txn)
    : table_heap_(table_heap), rid_(rid), txn_(txn) {}

TableIterator::TableIterator(const TableIterator &other)
    : table_heap_(other.table_heap_), rid_(other.rid_), txn_(other.txn_), row_(other.row_) {}

TableIterator::~TableIterator() {}

bool TableIterator::operator==(const TableIterator &itr) const {  // 堆表相等且rid相等
  return this->table_heap_ == itr.table_heap_ && this->rid_ == itr.rid_;
}

bool TableIterator::operator!=(const TableIterator &itr) const { return !(*this == itr); }

const Row &TableIterator::operator*() {
  if (rid_.GetPageId() == INVALID_PAGE_ID) {  // 无效rid，返回空行
    return row_;
  }
  row_.SetRowId(rid_);
  auto buffer_pool_manager = table_heap_->buffer_pool_manager_;
  auto schema = table_heap_->schema_;
  auto lock_manager = table_heap_->lock_manager_;
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager->FetchPage(rid_.GetPageId()));  // 拿到包含rid的页面
  if (page == nullptr) {
    return row_;
  }
  row_.destroy();  // 清理旧行数据
  bool success = page->GetTuple(&row_, schema, txn_, lock_manager);
  buffer_pool_manager->UnpinPage(rid_.GetPageId(), false);
  if (!success) {
    return row_;
  }
  row_.SetRowId(rid_);
  return row_;
}

Row *TableIterator::operator->() { return const_cast<Row *>(&this->operator*()); }

TableIterator &TableIterator::operator=(const TableIterator &itr) noexcept {
  if (this != &itr) {
    table_heap_ = itr.table_heap_;
    rid_ = itr.rid_;
    txn_ = itr.txn_;
    row_ = itr.row_;
  }
  return *this;
}

// ++iter
TableIterator &TableIterator::operator++() {
  if (rid_.GetPageId() == INVALID_PAGE_ID) {
    return *this;
  }
  auto buffer_pool_manager = table_heap_->buffer_pool_manager_;
  page_id_t current_page_id = rid_.GetPageId();
  // 获取当前页
  auto page = reinterpret_cast<TablePage *>(buffer_pool_manager->FetchPage(current_page_id));
  if (page == nullptr) {
    rid_ = INVALID_ROWID;
    return *this;
  }
  RowId next_rid;
  if (page->GetNextTupleRid(rid_, &next_rid)) {  // 当前页找到有效元组
    rid_ = next_rid;
    buffer_pool_manager->UnpinPage(current_page_id, false);
  } else {  // 找不到，继续遍历后续页
    buffer_pool_manager->UnpinPage(current_page_id, false);
    page_id_t next_page_id = page->GetNextPageId();
    bool found = false;
    while (next_page_id != INVALID_PAGE_ID) {
      auto next_page = reinterpret_cast<TablePage *>(buffer_pool_manager->FetchPage(next_page_id));
      if (next_page == nullptr) {
        break;
      }
      if (next_page->GetFirstTupleRid(&next_rid)) {
        rid_ = next_rid;
        buffer_pool_manager->UnpinPage(next_page_id, false);
        found = true;
        break;
      }
      buffer_pool_manager->UnpinPage(next_page_id, false);
      next_page_id = next_page->GetNextPageId();
    }
    if (!found) {  // 所有页面均未找到
      rid_ = INVALID_ROWID;
    }
  }
  return *this;
}

// iter++
TableIterator TableIterator::operator++(int) {  // 复用逻辑
  TableIterator tmp = *this;
  ++(*this);
  return tmp;
}