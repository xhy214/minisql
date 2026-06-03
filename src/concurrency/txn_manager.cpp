#include "concurrency/txn_manager.h"

#include "concurrency/lock_manager.h"

// 将事务与锁双向绑定
TxnManager::TxnManager(LockManager *lock_mgr) : lock_mgr_(lock_mgr) { lock_mgr_->SetTxnMgr(this); }

Txn *TxnManager::Begin(Txn *txn, IsolationLevel isolationLevel) {
  if (nullptr == txn) {  // 默认自增id
    txn = new Txn(next_txn_id_++, isolationLevel);
  }
  std::unique_lock<std::shared_mutex> lock(rw_latch_);  // 保护插入
  txn_map_[txn->GetTxnId()] = txn;
  return txn;
}

void TxnManager::Commit(Txn *txn) {
  // 先改变状态，在释放所有锁
  txn->SetState(TxnState::kCommitted);
  ReleaseLocks(txn);
}

void TxnManager::Abort(Txn *txn) {
  // 先改变状态，在释放所有锁
  txn->SetState(TxnState::kAborted);
  ReleaseLocks(txn);
}

Txn *TxnManager::GetTransaction(txn_id_t txn_id) {
  std::shared_lock<std::shared_mutex> lock(rw_latch_);
  auto iter = txn_map_.find(txn_id);
  if (iter != txn_map_.end()) {//按id查找事务
    return iter->second;
  }
  return nullptr;
}

void TxnManager::ReleaseLocks(Txn *txn) {
  std::unordered_set<RowId> lock_set;//使用set去重
  for (auto o : txn->GetExclusiveLockSet()) {
    lock_set.emplace(o);
  }
  for (auto o : txn->GetSharedLockSet()) {
    lock_set.emplace(o);
  }
  for (auto rid : lock_set) {
    lock_mgr_->Unlock(txn, rid);
  }
}