#ifndef MINISQL_TABLE_HEAP_H
#define MINISQL_TABLE_HEAP_H

#include "buffer/buffer_pool_manager.h"
#include "concurrency/lock_manager.h"
#include "page/header_page.h"
#include "page/table_page.h"
#include "recovery/log_manager.h"
#include "storage/table_iterator.h"

class TableHeap {
  friend class TableIterator;

 public:
  static TableHeap *Create(BufferPoolManager *buffer_pool_manager, Schema *schema, Txn *txn, LogManager *log_manager,
                           LockManager *lock_manager) {
    return new TableHeap(buffer_pool_manager, schema, txn, log_manager, lock_manager);
  }

  static TableHeap *Create(BufferPoolManager *buffer_pool_manager, page_id_t first_page_id, Schema *schema,
                           LogManager *log_manager, LockManager *lock_manager) {
    return new TableHeap(buffer_pool_manager, first_page_id, schema, log_manager, lock_manager);
  }

  ~TableHeap() {}

  /**
   * 向表中插入一个元组。如果元组过大（大于等于页面大小），则返回 false。
   * @param[in/out] row 待插入的元组行，插入后生成的记录ID (rid) 会被封装回 row 对象中
   * @param[in] txn 执行插入操作的事务
   * @return 仅当插入成功时返回 true
   */
  bool InsertTuple(Row &row, Txn *txn);

 /**
   * 将元组标记为已删除。实际的物理删除将在调用 ApplyDelete 时发生。
   * @param[in] rid 待删除元组的资源标识符 (RID)
   * @param[in] txn 执行删除操作的事务
   * @return 仅当删除成功（即元组确实存在）时返回 true
   */
  bool MarkDelete(const RowId &rid, Txn *txn);

  /**
   * 如果新元组过大，无法放入旧页面中，则返回 false（此时将转为执行删除旧元组并插入新元组的操作）。
   * @param[in] row 新行的元组数据
   * @param[in] rid 旧元组的资源标识符 (RID)
   * @param[in] txn 执行更新操作的事务
   * @return 如果更新成功则返回 true。
   */
  bool UpdateTuple(Row &row, const RowId &rid, Txn *txn);

  /**
   * 在事务提交或中止时调用，用于真正物理删除元组或回滚插入操作。
   * @param rid 待删除元组的资源标识符 (RID)
   * @param txn 执行删除操作的事务
   */
  void ApplyDelete(const RowId &rid, Txn *txn);

  /**
   * 在事务中止时调用，用于回滚删除操作。
   * @param[in] rid 被删除元组的资源标识符 (RID)
   * @param[in] txn 执行回滚操作的事务
   */
  void RollbackDelete(const RowId &rid, Txn *txn);

  /**
   * 从表中读取一个元组。
   * @param[in/out] row 用于输出元组的变量，元组的行ID (RID) 会被封装在 row 对象中
   * @param[in] txn 执行读取操作的事务
   * @return 如果读取成功（即元组存在），则返回 true
   */
  bool GetTuple(Row *row, Txn *txn);

  void FreeTableHeap() {
    auto next_page_id = first_page_id_;
    while (next_page_id != INVALID_PAGE_ID) {
      auto old_page_id = next_page_id;
      auto page = reinterpret_cast<TablePage *>(buffer_pool_manager_->FetchPage(old_page_id));
      assert(page != nullptr);
      next_page_id = page->GetNextPageId();
      buffer_pool_manager_->UnpinPage(old_page_id, false);
      buffer_pool_manager_->DeletePage(old_page_id);
    }
  }

  /**
   * Free table heap and release storage in disk file
   */
  void DeleteTable(page_id_t page_id = INVALID_PAGE_ID);

  /**
   * @return the begin iterator of this table
   */
  TableIterator Begin(Txn *txn);

  /**
   * @return the end iterator of this table
   */
  TableIterator End();

  /**
   * @return the id of the first page of this table
   */
  inline page_id_t GetFirstPageId() const { return first_page_id_; }

 private:
  /**
   * create table heap and initialize first page
   */
  TableHeap(BufferPoolManager *buffer_pool_manager, Schema *schema, Txn *txn, LogManager *log_manager,
                     LockManager *lock_manager);

  explicit TableHeap(BufferPoolManager *buffer_pool_manager, page_id_t first_page_id, Schema *schema,
                     LogManager *log_manager, LockManager *lock_manager)
      : buffer_pool_manager_(buffer_pool_manager),
        first_page_id_(first_page_id),
        first_free_page_id_(first_page_id),
        schema_(schema),
        log_manager_(log_manager),
        lock_manager_(lock_manager) {}

 private:
  BufferPoolManager *buffer_pool_manager_;
  page_id_t first_page_id_;
  page_id_t first_free_page_id_{INVALID_PAGE_ID};
  Schema *schema_;
  [[maybe_unused]] LogManager *log_manager_;
  [[maybe_unused]] LockManager *lock_manager_;
};

#endif  // MINISQL_TABLE_HEAP_H
