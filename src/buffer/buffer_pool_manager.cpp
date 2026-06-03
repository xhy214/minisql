#include "buffer/buffer_pool_manager.h"

#include "glog/logging.h"
#include "page/bitmap_page.h"

static const char EMPTY_PAGE_DATA[PAGE_SIZE] = {0};

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
  pages_ = new Page[pool_size_];
  replacer_ = new LRUReplacer(pool_size_);
  for (size_t i = 0; i < pool_size_; i++) {
    free_list_.emplace_back(i);
  }
}

BufferPoolManager::~BufferPoolManager() {
  for (auto page : page_table_) {
    FlushPage(page.first);
  }
  delete[] pages_;
  delete replacer_;
}

/**
 * TODO: Student Implement
 */
Page *BufferPoolManager::FetchPage(page_id_t page_id) {
  // 在页表中搜索请求的页面
  auto it = page_table_.find(page_id);
  Page *page = nullptr;
  if (it != page_table_.end()) {  // 如果 P 存在，将其固定并立即返回
    page = &pages_[it->second];
    page->pin_count_++;
    replacer_->Pin(it->second);
  }
  // 如果 P 不存在，寻找一个替换页
  else {
    frame_id_t frame_id = INVALID_FRAME_ID;
    if (!free_list_.empty()) {  // 优先从空闲列表取
      frame_id = free_list_.back();
      free_list_.pop_back();
      page = &pages_[frame_id];
    } else {  // 调用替换器获取
      if (!replacer_->Victim(&frame_id)) {
        return nullptr;
      }
      page = &pages_[frame_id];
      if (page->is_dirty_) {  // 如果 R 是脏页，刷页
        disk_manager_->WritePage(page->page_id_, page->GetData());
        page->is_dirty_ = false;
      }
      page_table_.erase(page->page_id_);  // 删去 R 的映射
    }
    // 更新 P 的元数据，从磁盘读取页面内容，然后返回指向 P 的指针
    page_table_[page_id] = frame_id;
    page->page_id_ = page_id;
    page->is_dirty_ = false;
    page->pin_count_ = 1;
    disk_manager_->ReadPage(page_id, page->GetData());
    replacer_->Pin(frame_id);
  }
  return page;
}

/**
 * TODO: Student Implement
 */
Page *BufferPoolManager::NewPage(page_id_t &page_id) {
  Page *page = nullptr;
  frame_id_t frame_id = INVALID_FRAME_ID;
  if (!free_list_.empty()) {  // 优先从空闲列表取
    frame_id = free_list_.back();
    free_list_.pop_back();
    page = &pages_[frame_id];
  } else {  // 调用替换器获取
    if (!replacer_->Victim(&frame_id)) {
      return nullptr;
    }
    page = &pages_[frame_id];
    if (page->is_dirty_) {  // 刷页
      disk_manager_->WritePage(page->page_id_, page->GetData());
    }
    page_table_.erase(page->page_id_);  // 删去 R 的映射
  }
  page_id = disk_manager_->AllocatePage();  // 分配物理页面
  // 更新 P 的元数据，将内存清零，并将 P 添加到页表
  page->ResetMemory();
  page_table_[page_id] = frame_id;
  page->page_id_ = page_id;
  page->is_dirty_ = false;
  page->pin_count_ = 1;
  replacer_->Pin(frame_id);
  return page;
}

/**
 * TODO: Student Implement
 */
bool BufferPoolManager::DeletePage(page_id_t page_id) {
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return true;
  }
  frame_id_t frame_id = it->second;
  Page *page = &pages_[frame_id];
  if (page->pin_count_ != 0) {  // Pin不为0，拒绝删除
    return false;
  }
  // 将 P 从页表中移除，重置其元数据，并将其归还到空闲列表。
  page_table_.erase(page_id);
  page->ResetMemory();
  replacer_->Pin(frame_id);
  page->page_id_ = INVALID_PAGE_ID;
  page->is_dirty_ = false;
  page->pin_count_ = 0;
  free_list_.emplace_back(frame_id);
  disk_manager_->DeAllocatePage(page_id);
  return true;
}

/**
 * TODO: Student Implement
 */
bool BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {  // 页不在页表中
    return false;
  }
  frame_id_t frame_id = it->second;
  Page *page = &pages_[frame_id];
  if (page->pin_count_ == 0) {  // Pin为0，拒绝解固定
    return false;
  }
  page->pin_count_--;
  if (page->pin_count_ == 0) {
    replacer_->Unpin(frame_id);
  }
  page->is_dirty_ |= is_dirty;  // 脏标志和原状态一致
  return true;
}

/**
 * TODO: Student Implement
 */
bool BufferPoolManager::FlushPage(page_id_t page_id) {  // 刷页
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }
  frame_id_t frame_id = it->second;
  Page *page = &pages_[frame_id];
  disk_manager_->WritePage(page_id, page->GetData());
  page->is_dirty_ = false;
  return true;
}

page_id_t BufferPoolManager::AllocatePage() {
  int next_page_id = disk_manager_->AllocatePage();
  return next_page_id;
}

void BufferPoolManager::DeallocatePage(__attribute__((unused)) page_id_t page_id) {
  disk_manager_->DeAllocatePage(page_id);
}

bool BufferPoolManager::IsPageFree(page_id_t page_id) { return disk_manager_->IsPageFree(page_id); }

// Only used for debug
bool BufferPoolManager::CheckAllUnpinned() {
  bool res = true;
  for (size_t i = 0; i < pool_size_; i++) {
    if (pages_[i].pin_count_ != 0) {
      res = false;
      LOG(ERROR) << "page " << pages_[i].page_id_ << " pin count:" << pages_[i].pin_count_ << endl;
    }
  }
  return res;
}