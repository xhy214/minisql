#ifndef MINISQL_LOG_REC_H
#define MINISQL_LOG_REC_H

#include <unordered_map>
#include <utility>

#include "common/config.h"
#include "common/rowid.h"
#include "record/row.h"

enum class LogRecType {
  kInvalid,
  kInsert,
  kDelete,
  kUpdate,
  kBegin,
  kCommit,
  kAbort,
};

// used for testing only
using KeyType = std::string;
using ValType = int32_t;

/**
 * TODO: Student Implement
 */
struct LogRec {  // 初始化字段为默认或无效值
  LogRec() = default;

  LogRecType type_{LogRecType::kInvalid};
  lsn_t lsn_{INVALID_LSN};
  lsn_t prev_lsn_{INVALID_LSN};
  txn_id_t txn_id_{INVALID_TXN_ID};

  KeyType insert_key_;
  ValType insert_val_{0};
  KeyType delete_key_;
  ValType delete_val_{0};
  KeyType update_old_key_;
  ValType update_old_val_{0};
  KeyType update_new_key_;
  ValType update_new_val_{0};
  /* used for testing only */
  static std::unordered_map<txn_id_t, lsn_t> prev_lsn_map_;
  static lsn_t next_lsn_;
};

std::unordered_map<txn_id_t, lsn_t> LogRec::prev_lsn_map_ = {};
lsn_t LogRec::next_lsn_ = 0;

typedef std::shared_ptr<LogRec> LogRecPtr;

/**
 * TODO: Student Implement
 */
static LogRecPtr CreateInsertLog(txn_id_t txn_id, KeyType ins_key, ValType ins_val) {  // 创建插入日志
  auto log_rec = std::make_shared<LogRec>();
  log_rec->type_ = LogRecType::kInsert;
  // 绑定事务并插入lsn链表
  log_rec->txn_id_ = txn_id;
  auto it = LogRec::prev_lsn_map_.find(txn_id);
  log_rec->prev_lsn_ = (it == LogRec::prev_lsn_map_.end()) ? INVALID_LSN : it->second;
  log_rec->lsn_ = LogRec::next_lsn_++;
  log_rec->insert_key_ = std::move(ins_key);
  log_rec->insert_val_ = ins_val;
  LogRec::prev_lsn_map_[txn_id] = log_rec->lsn_;
  return log_rec;
}

/**
 * TODO: Student Implement
 */
static LogRecPtr CreateDeleteLog(txn_id_t txn_id, KeyType del_key, ValType del_val) {  // 创建删除日志
  auto log_rec = std::make_shared<LogRec>();
  log_rec->type_ = LogRecType::kDelete;
  log_rec->txn_id_ = txn_id;
  auto it = LogRec::prev_lsn_map_.find(txn_id);
  log_rec->prev_lsn_ = (it == LogRec::prev_lsn_map_.end()) ? INVALID_LSN : it->second;
  log_rec->lsn_ = LogRec::next_lsn_++;
  log_rec->delete_key_ = std::move(del_key);
  log_rec->delete_val_ = del_val;
  LogRec::prev_lsn_map_[txn_id] = log_rec->lsn_;
  return log_rec;
}

/**
 * TODO: Student Implement
 */
static LogRecPtr CreateUpdateLog(txn_id_t txn_id, KeyType old_key, ValType old_val, KeyType new_key,
                                 ValType new_val) {  // 创建更新日志
  auto log_rec = std::make_shared<LogRec>();
  log_rec->type_ = LogRecType::kUpdate;
  log_rec->txn_id_ = txn_id;
  auto it = LogRec::prev_lsn_map_.find(txn_id);
  log_rec->prev_lsn_ = (it == LogRec::prev_lsn_map_.end()) ? INVALID_LSN : it->second;
  log_rec->lsn_ = LogRec::next_lsn_++;
  log_rec->update_old_key_ = std::move(old_key);
  log_rec->update_old_val_ = old_val;
  log_rec->update_new_key_ = std::move(new_key);
  log_rec->update_new_val_ = new_val;
  LogRec::prev_lsn_map_[txn_id] = log_rec->lsn_;
  return log_rec;
}

/**
 * TODO: Student Implement
 */
static LogRecPtr CreateBeginLog(txn_id_t txn_id) {  // 事务开始日志
  auto log_rec = std::make_shared<LogRec>();
  log_rec->type_ = LogRecType::kBegin;
  log_rec->txn_id_ = txn_id;
  log_rec->prev_lsn_ = INVALID_LSN;
  log_rec->lsn_ = LogRec::next_lsn_++;
  LogRec::prev_lsn_map_[txn_id] = log_rec->lsn_;
  return log_rec;
}

/**
 * TODO: Student Implement
 */
static LogRecPtr CreateCommitLog(txn_id_t txn_id) {  // 事务提交日志
  auto log_rec = std::make_shared<LogRec>();
  log_rec->type_ = LogRecType::kCommit;
  log_rec->txn_id_ = txn_id;
  auto it = LogRec::prev_lsn_map_.find(txn_id);
  log_rec->prev_lsn_ = (it == LogRec::prev_lsn_map_.end()) ? INVALID_LSN : it->second;
  log_rec->lsn_ = LogRec::next_lsn_++;
  LogRec::prev_lsn_map_[txn_id] = log_rec->lsn_;
  return log_rec;
}

/**
 * TODO: Student Implement
 */
static LogRecPtr CreateAbortLog(txn_id_t txn_id) {  // 事务中止日志
  auto log_rec = std::make_shared<LogRec>();
  log_rec->type_ = LogRecType::kAbort;
  log_rec->txn_id_ = txn_id;
  auto it = LogRec::prev_lsn_map_.find(txn_id);
  log_rec->prev_lsn_ = (it == LogRec::prev_lsn_map_.end()) ? INVALID_LSN : it->second;
  log_rec->lsn_ = LogRec::next_lsn_++;
  LogRec::prev_lsn_map_[txn_id] = log_rec->lsn_;
  return log_rec;
}

#endif  // MINISQL_LOG_REC_H
