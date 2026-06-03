#include "concurrency/lock_manager.h"

#include <iostream>

#include "common/rowid.h"
#include "concurrency/txn.h"
#include "concurrency/txn_manager.h"

void LockManager::SetTxnMgr(TxnManager *txn_mgr) { txn_mgr_ = txn_mgr; }

/**
 * TODO: Student Implement
 */
bool LockManager::LockShared(Txn *txn, const RowId &rid) {
  // 逻辑上不加共享锁，直接抛出错误
  if (txn->GetIsolationLevel() == IsolationLevel::kReadUncommitted) {
    txn->SetState(TxnState::kAborted);
    throw TxnAbortException(txn->GetTxnId(), AbortReason::kLockSharedOnReadUncommitted);
  }
  std::unique_lock<std::mutex> lock(latch_);  // 拿到全局互斥锁
  LockPrepare(txn, rid);                      // 合法性检查
  LockRequestQueue &req_queue = lock_table_[rid];
  req_queue.EmplaceLockRequest(txn->GetTxnId(), LockMode::kShared);  // 加入该id的锁请求队列
  while (req_queue.is_writing_ || req_queue.is_upgrading_) {         // 等待正在写或升级的事务
    CheckAbort(txn, req_queue);                                      // 检查是否被牺牲
    req_queue.cv_.wait(lock);
  }
  auto it = req_queue.GetLockRequestIter(txn->GetTxnId());
  it->granted_ = LockMode::kShared;  // 授予共享锁
  req_queue.sharing_cnt_++;          // 计数加一
  if (txn->GetIsolationLevel() == IsolationLevel::kRepeatedRead) {
    txn->GetSharedLockSet().emplace(rid);  // 可重复读需要持有锁直到结束
  }
  return true;
}

/**
 * TODO: Student Implement
 */
bool LockManager::LockExclusive(Txn *txn, const RowId &rid) {
  std::unique_lock<std::mutex> lock(latch_);
  LockPrepare(txn, rid);
  LockRequestQueue &req_queue = lock_table_[rid];
  req_queue.EmplaceLockRequest(txn->GetTxnId(), LockMode::kExclusive);
  while (req_queue.is_writing_ || req_queue.is_upgrading_ || req_queue.sharing_cnt_ > 0) {  // 还需要等读者全部释放
    CheckAbort(txn, req_queue);
    req_queue.cv_.wait(lock);
  }
  auto it = req_queue.GetLockRequestIter(txn->GetTxnId());
  it->granted_ = LockMode::kExclusive;      // 授予排他锁
  req_queue.is_writing_ = true;             // 置写标记
  txn->GetExclusiveLockSet().emplace(rid);  // 无条件持有直到事务结束
  return true;
}

/**
 * TODO: Student Implement
 */
bool LockManager::LockUpgrade(Txn *txn, const RowId &rid) {
  std::unique_lock<std::mutex> lock(latch_);
  LockPrepare(txn, rid);  // 合法性检查
  LockRequestQueue &req_queue = lock_table_[rid];
  CheckAbort(txn, req_queue);  // 检查是否被牺牲
  auto it = req_queue.GetLockRequestIter(txn->GetTxnId());
  if (it->granted_ != LockMode::kShared || req_queue.is_upgrading_) {  // 未持有共享锁或有其他升级事务存在，抛出错误
    txn->SetState(TxnState::kAborted);
    throw TxnAbortException(txn->GetTxnId(), AbortReason::kUpgradeConflict);
  }
  req_queue.is_upgrading_ = true;       // 置升级标记
  while (req_queue.sharing_cnt_ > 1) {  // 等待其他读者释放
    CheckAbort(txn, req_queue);
    req_queue.cv_.wait(lock);
  }
  CheckAbort(txn, req_queue);           // 防止cnt=1跳过检查
  it->granted_ = LockMode::kExclusive;  // 授予排他锁
  req_queue.is_upgrading_ = false;      // 清除升级标记
  req_queue.is_writing_ = true;         // 置写标记
  req_queue.sharing_cnt_ = 0;           // 置计数为0
  txn->GetSharedLockSet().erase(rid);
  txn->GetExclusiveLockSet().emplace(rid);
  return true;
}

/**
 * TODO: Student Implement
 */
bool LockManager::Unlock(Txn *txn, const RowId &rid) {
  std::unique_lock<std::mutex> lock(latch_);
  auto table_it = lock_table_.find(rid);
  if (table_it == lock_table_.end()) {  // 锁不存在，返回失败
    return false;
  }
  LockRequestQueue &req_queue = table_it->second;
  auto req_it = req_queue.GetLockRequestIter(txn->GetTxnId());
  LockMode granted = req_it->granted_;
  if (granted == LockMode::kNone) {  // 在请求队列但尚未授予锁
    return false;
  }
  if (granted == LockMode::kExclusive) {  // 排他锁清除写标志
    req_queue.is_writing_ = false;
    txn->GetExclusiveLockSet().erase(rid);
  } else {  // 共享锁减少计数
    req_queue.sharing_cnt_--;
    txn->GetSharedLockSet().erase(rid);
  }
  req_queue.EraseLockRequest(txn->GetTxnId());  // 从请求队列中清除
  if (txn->GetState() == TxnState::kGrowing) {  // 2PL状态转变
    txn->SetState(TxnState::kShrinking);
  }
  req_queue.cv_.notify_all();  // 通知等待的线程
  return true;
}

/**
 * TODO: Student Implement
 */
void LockManager::LockPrepare(Txn *txn, const RowId &rid) {
  if (txn->GetState() == TxnState::kShrinking) {  // 不允许在2PL收缩期申请
    txn->SetState(TxnState::kAborted);
    throw TxnAbortException(txn->GetTxnId(), AbortReason::kLockOnShrinking);
  }
  if (txn->GetState() == TxnState::kAborted) {  // 不允许已中止事务申请
    throw TxnAbortException(txn->GetTxnId(), AbortReason::kDeadlock);
  }
  if (lock_table_.find(rid) == lock_table_.end()) {  // 初始化锁队列
    lock_table_.try_emplace(rid);
  }
}

/**
 * TODO: Student Implement
 */
void LockManager::CheckAbort(Txn *txn, LockManager::LockRequestQueue &req_queue) {
  if (txn->GetState() == TxnState::kAborted) {  // 不允许已中止事务申请
    req_queue.EraseLockRequest(txn->GetTxnId());
    throw TxnAbortException(txn->GetTxnId(), AbortReason::kDeadlock);
  }
}

/**
 * TODO: Student Implement
 */
void LockManager::AddEdge(txn_id_t t1, txn_id_t t2) { waits_for_[t1].emplace(t2); }

/**
 * TODO: Student Implement
 */
void LockManager::RemoveEdge(txn_id_t t1, txn_id_t t2) {
  auto it = waits_for_.find(t1);
  if (it != waits_for_.end()) {
    it->second.erase(t2);
    if (it->second.empty()) {
      waits_for_.erase(it);
    }
  }
}

/**
 * TODO: Student Implement
 */
bool LockManager::HasCycle(txn_id_t &newest_tid_in_cycle) {
  // 收集所有节点，按升序排列
  std::vector<txn_id_t> nodes;
  for (const auto &[tid, _] : waits_for_) {
    nodes.push_back(tid);
  }
  std::sort(nodes.begin(), nodes.end());
  // 重置辅助容器
  visited_set_.clear();
  std::unordered_set<txn_id_t> in_progress;
  std::vector<txn_id_t> path;
  std::function<bool(txn_id_t)> dfs = [&](txn_id_t node) -> bool {
    visited_set_.insert(node);
    in_progress.insert(node);
    path.push_back(node);
    auto it = waits_for_.find(node);
    if (it != waits_for_.end()) {
      std::vector<txn_id_t> neighbors(it->second.begin(), it->second.end());
      std::sort(neighbors.begin(), neighbors.end());
      for (txn_id_t next : neighbors) {
        if (in_progress.count(next)) {  // 遇到活跃节点，成环
          auto pos = std::find(path.begin(), path.end(), next);
          newest_tid_in_cycle = *std::max_element(pos, path.end());  // 去除id最大的事务牺牲
          return true;
        }
        if (!visited_set_.count(next)) {
          if (dfs(next)) return true;
        }
      }
    }
    path.pop_back();
    in_progress.erase(node);
    return false;
  };
  for (txn_id_t node : nodes) {  // 外层调用lambda
    if (!visited_set_.count(node)) {
      if (dfs(node)) return true;
    }
  }
  return false;
}

void LockManager::DeleteNode(txn_id_t txn_id) {
  waits_for_.erase(txn_id);  // 等待图中删除自己
  auto *txn = txn_mgr_->GetTransaction(txn_id);
  // 删除所有等待且未授予的请求
  for (const auto &row_id : txn->GetSharedLockSet()) {
    for (const auto &lock_req : lock_table_[row_id].req_list_) {
      if (lock_req.granted_ == LockMode::kNone) {
        RemoveEdge(lock_req.txn_id_, txn_id);
      }
    }
  }
  for (const auto &row_id : txn->GetExclusiveLockSet()) {
    for (const auto &lock_req : lock_table_[row_id].req_list_) {
      if (lock_req.granted_ == LockMode::kNone) {
        RemoveEdge(lock_req.txn_id_, txn_id);
      }
    }
  }
}

/**
 * TODO: Student Implement
 */
void LockManager::RunCycleDetection() {
  while (enable_cycle_detection_) {  // 后台周期性检查死锁
    std::this_thread::sleep_for(cycle_detection_interval_);
    if (!enable_cycle_detection_) break;
    std::unique_lock<std::mutex> lock(latch_);  // 获取全局锁
    // 从零构建等待图
    waits_for_.clear();
    for (auto &[rid, req_queue] : lock_table_) {
      std::vector<txn_id_t> holders;
      for (auto &req : req_queue.req_list_) {
        if (req.granted_ != LockMode::kNone) {
          Txn *holder = txn_mgr_->GetTransaction(req.txn_id_);
          if (holder != nullptr && holder->GetState() != TxnState::kAborted) {  // 收集所有有效持有者
            holders.push_back(req.txn_id_);
          }
        }
      }
      for (auto &req : req_queue.req_list_) {
        if (req.granted_ != LockMode::kNone) continue;  // 跳过持有者
        Txn *waiter = txn_mgr_->GetTransaction(req.txn_id_);
        if (waiter == nullptr || waiter->GetState() == TxnState::kAborted) continue;
        for (txn_id_t holder : holders) {
          if (req.txn_id_ != holder) {  // 建立等待--持有边
            AddEdge(req.txn_id_, holder);
          }
        }
      }
    }
    txn_id_t victim;
    while (HasCycle(victim)) {
      Txn *victim_txn = txn_mgr_->GetTransaction(victim);
      if (victim_txn != nullptr) {
        victim_txn->SetState(TxnState::kAborted);  // 标记中止
        for (auto &[rid, req_queue] : lock_table_) {
          for (auto &req : req_queue.req_list_) {
            if (req.txn_id_ == victim) {
              req_queue.cv_.notify_all();  // 唤醒所有等待该id的事务
              break;
            }
          }
        }
      }
      DeleteNode(victim);  // 删除该id节点
    }
  }
}

/**
 * TODO: Student Implement
 */
std::vector<std::pair<txn_id_t, txn_id_t>> LockManager::GetEdgeList() {
  std::vector<std::pair<txn_id_t, txn_id_t>> result;
  for (const auto &[t1, neighbors] : waits_for_) {  // 取出所有等待对
    for (txn_id_t t2 : neighbors) {
      result.emplace_back(t1, t2);
    }
  }
  return result;
}
