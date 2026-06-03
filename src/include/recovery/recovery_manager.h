#ifndef MINISQL_RECOVERY_MANAGER_H
#define MINISQL_RECOVERY_MANAGER_H

#include <map>
#include <unordered_map>
#include <vector>

#include "recovery/log_rec.h"

using KvDatabase = std::unordered_map<KeyType, ValType>;
using ATT = std::unordered_map<txn_id_t, lsn_t>;

struct CheckPoint {
  lsn_t checkpoint_lsn_{INVALID_LSN};
  ATT active_txns_{};
  KvDatabase persist_data_{};

  inline void AddActiveTxn(txn_id_t txn_id, lsn_t last_lsn) { active_txns_[txn_id] = last_lsn; }

  inline void AddData(KeyType key, ValType val) { persist_data_.emplace(std::move(key), val); }
};

class RecoveryManager {
 public:
  /**
   * TODO: Student Implement
   */
  void Init(CheckPoint &last_checkpoint) {  // 从最近检查点初始化
    persist_lsn_ = last_checkpoint.checkpoint_lsn_;
    active_txns_ = last_checkpoint.active_txns_;
    data_ = last_checkpoint.persist_data_;
  }

  /**
   * TODO: Student Implement
   */
  void RedoPhase() {                                 // 重新应用操作并重建ATT
    for (auto &[txn_id, last_lsn] : active_txns_) {  // 遍历所有非持久化事务并还原
      lsn_t lsn = last_lsn;
      while (lsn != INVALID_LSN && lsn <= persist_lsn_) {
        auto &log = log_recs_[lsn];
        switch (log->type_) {
          case LogRecType::kInsert:
            data_.erase(log->insert_key_);
            break;
          case LogRecType::kDelete:
            data_[log->delete_key_] = log->delete_val_;
            break;
          case LogRecType::kUpdate:
            data_[log->update_old_key_] = log->update_old_val_;
            break;
          default:
            break;
        }
        lsn = log->prev_lsn_;
      }
    }
    for (auto &[lsn, log] : log_recs_) {  // 正向扫描并重做
      if (lsn <= persist_lsn_) {          // 忽略所有已持久化日志
        continue;
      }
      switch (log->type_) {
        case LogRecType::kInsert:
          data_[log->insert_key_] = log->insert_val_;
          break;
        case LogRecType::kDelete:
          data_.erase(log->delete_key_);
          break;
        case LogRecType::kUpdate:
          data_[log->update_new_key_] = log->update_new_val_;
          break;
        case LogRecType::kBegin:
          active_txns_[log->txn_id_] = log->lsn_;
          continue;
        case LogRecType::kCommit:
        case LogRecType::kAbort:
          active_txns_.erase(log->txn_id_);
          continue;
        default:
          continue;
      }
      if (active_txns_.count(log->txn_id_)) {//更新ATT
        active_txns_[log->txn_id_] = lsn;
      }
    }
  }

  /**
   * TODO: Student Implement
   */
  void UndoPhase() {  // 沿着链表反向撤销所有操作
    for (auto &[txn_id, last_lsn] : active_txns_) {
      lsn_t lsn = last_lsn;
      while (lsn != INVALID_LSN) {
        auto &log = log_recs_[lsn];
        switch (log->type_) {
          case LogRecType::kInsert:
            data_.erase(log->insert_key_);
            break;
          case LogRecType::kDelete:
            data_[log->delete_key_] = log->delete_val_;
            break;
          case LogRecType::kUpdate:
            data_[log->update_old_key_] = log->update_old_val_;
            break;
          case LogRecType::kBegin:
            lsn = INVALID_LSN;
            continue;
          default:
            break;
        }
        lsn = log->prev_lsn_;
      }
    }
  }

  // used for test only
  void AppendLogRec(LogRecPtr log_rec) { log_recs_.emplace(log_rec->lsn_, log_rec); }

  // used for test only
  inline KvDatabase &GetDatabase() { return data_; }

 private:
  std::map<lsn_t, LogRecPtr> log_recs_{};
  lsn_t persist_lsn_{INVALID_LSN};
  ATT active_txns_{};
  KvDatabase data_{};  // all data in database
};

#endif  // MINISQL_RECOVERY_MANAGER_H
