#include "storage/disk_manager.h"

#include <sys/stat.h>

#include <filesystem>
#include <stdexcept>

#include "glog/logging.h"
#include "page/bitmap_page.h"

DiskManager::DiskManager(const std::string &db_file) : file_name_(db_file) {
  std::scoped_lock<std::recursive_mutex> lock(db_io_latch_);
  db_io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
  // directory or file does not exist
  if (!db_io_.is_open()) {
    db_io_.clear();
    // create a new file
    std::filesystem::path p = db_file;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    db_io_.open(db_file, std::ios::binary | std::ios::trunc | std::ios::out);
    db_io_.close();
    // reopen with original mode
    db_io_.open(db_file, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
      throw std::exception();
    }
  }
  ReadPhysicalPage(META_PAGE_ID, meta_data_);
}

void DiskManager::Close() {
  std::scoped_lock<std::recursive_mutex> lock(db_io_latch_);
  WritePhysicalPage(META_PAGE_ID, meta_data_);
  if (!closed) {
    db_io_.close();
    closed = true;
  }
}

void DiskManager::ReadPage(page_id_t logical_page_id, char *page_data) {
  ASSERT(logical_page_id >= 0, "Invalid page id.");
  ReadPhysicalPage(MapPageId(logical_page_id), page_data);
}

void DiskManager::WritePage(page_id_t logical_page_id, const char *page_data) {
  ASSERT(logical_page_id >= 0, "Invalid page id.");
  WritePhysicalPage(MapPageId(logical_page_id), page_data);
}

/**
 * TODO: Student Implement
 */
page_id_t DiskManager::AllocatePage() {
  DiskFileMetaPage *meta_page = reinterpret_cast<DiskFileMetaPage *>(meta_data_);
  if (meta_page->num_allocated_pages_ >= MAX_VALID_PAGE_ID) {  // 无法分配更多页
    return INVALID_PAGE_ID;
  }
  page_id_t logical_id_get = INVALID_PAGE_ID;
  if (meta_page->num_allocated_pages_ >= meta_page->num_extents_ * BITMAP_SIZE) {  // 当前所有区均满，分配新区
    meta_page->num_extents_++;
    meta_page->extent_used_page_[meta_page->num_extents_ - 1] = 1;
    memset(buffer_, 0, PAGE_SIZE);                                                  // 清空缓存区
    BitmapPage<4096> *bitmap_page = reinterpret_cast<BitmapPage<4096> *>(buffer_);  // 转为位图结构
    uint32_t page_offset = 0;
    bitmap_page->AllocatePage(page_offset);  // 得到在新位图中的偏移量
    uint32_t bitmap_page_id = 1 + (meta_page->num_extents_ - 1) * (1 + BITMAP_SIZE);
    WritePhysicalPage(bitmap_page_id, buffer_);        // 写入位图页
    logical_id_get = meta_page->num_allocated_pages_;  // 返回新分配的逻辑页号
  } else {
    for (uint32_t i = 0; i < meta_page->num_extents_; ++i) {
      if (meta_page->extent_used_page_[i] < BITMAP_SIZE) {  // 有分区未满
        meta_page->extent_used_page_[i]++;
        uint32_t bitmap_page_id = 1 + i * (1 + BITMAP_SIZE);
        ReadPhysicalPage(bitmap_page_id, buffer_);
        BitmapPage<4096> *bitmap_page = reinterpret_cast<BitmapPage<4096> *>(buffer_);  // 转为位图结构
        uint32_t page_offset = 0;
        bitmap_page->AllocatePage(page_offset);
        WritePhysicalPage(bitmap_page_id, buffer_);  // 写回改动的位图页
        logical_id_get = meta_page->num_allocated_pages_;
        break;
      }
    }
  }
  meta_page->num_allocated_pages_++;
  WritePhysicalPage(META_PAGE_ID, meta_data_);  // 更新元数据
  return logical_id_get;
}

/**
 * TODO: Student Implement
 */
void DiskManager::DeAllocatePage(page_id_t logical_page_id) {
  DiskFileMetaPage *meta_page = reinterpret_cast<DiskFileMetaPage *>(meta_data_);
  uint32_t extent_id = logical_page_id / BITMAP_SIZE;
  if (meta_page->num_extents_ <= extent_id) {  // 分区号不存在
    return;
  }
  uint32_t offset_in_extent = logical_page_id % BITMAP_SIZE;
  uint32_t bitmap_id = extent_id * (1 + BITMAP_SIZE) + 1;
  ReadPhysicalPage(bitmap_id, buffer_);
  BitmapPage<4096> *bitmap_page = reinterpret_cast<BitmapPage<4096> *>(buffer_);
  bitmap_page->DeAllocatePage(offset_in_extent);
  meta_page->extent_used_page_[extent_id]--;
  meta_page->num_allocated_pages_--;
  WritePhysicalPage(bitmap_id, buffer_);
  WritePhysicalPage(META_PAGE_ID, meta_data_);
}

/**
 * TODO: Student Implement
 */
bool DiskManager::IsPageFree(page_id_t logical_page_id) {
  DiskFileMetaPage *meta_page = reinterpret_cast<DiskFileMetaPage *>(meta_data_);
  uint32_t extent_id = logical_page_id / BITMAP_SIZE;
  if (meta_page->num_extents_ <= extent_id) {  // 分区不存在，页面从未被分配
    return true;
  }
  uint32_t offset_in_extent = logical_page_id % BITMAP_SIZE;
  uint32_t bitmap_id = extent_id * (1 + BITMAP_SIZE) + 1;//计算对应位图的物理id
  ReadPhysicalPage(bitmap_id, buffer_);
  BitmapPage<4096> *bitmap_page = reinterpret_cast<BitmapPage<4096> *>(buffer_);
  return bitmap_page->IsPageFree(offset_in_extent);
}

/**
 * TODO: Student Implement
 */
page_id_t DiskManager::MapPageId(page_id_t logical_page_id) {//逻辑页号转物理页号
  uint32_t extent_id = logical_page_id / BITMAP_SIZE;
  uint32_t group_offset = extent_id * (1 + BITMAP_SIZE);
  uint32_t offset_in_extent = 1 + logical_page_id % BITMAP_SIZE;
  return 1 + group_offset + offset_in_extent;
}

int DiskManager::GetFileSize(const std::string &file_name) {
  struct stat stat_buf;
  int rc = stat(file_name.c_str(), &stat_buf);
  return rc == 0 ? stat_buf.st_size : -1;
}

void DiskManager::ReadPhysicalPage(page_id_t physical_page_id, char *page_data) {
  int offset = physical_page_id * PAGE_SIZE;
  // check if read beyond file length
  if (offset >= GetFileSize(file_name_)) {
#ifdef ENABLE_BPM_DEBUG
    LOG(INFO) << "Read less than a page" << std::endl;
#endif
    memset(page_data, 0, PAGE_SIZE);
  } else {
    // set read cursor to offset
    db_io_.seekp(offset);
    db_io_.read(page_data, PAGE_SIZE);
    // if file ends before reading PAGE_SIZE
    int read_count = db_io_.gcount();
    if (read_count < PAGE_SIZE) {
#ifdef ENABLE_BPM_DEBUG
      LOG(INFO) << "Read less than a page" << std::endl;
#endif
      memset(page_data + read_count, 0, PAGE_SIZE - read_count);
    }
  }
}

void DiskManager::WritePhysicalPage(page_id_t physical_page_id, const char *page_data) {
  size_t offset = static_cast<size_t>(physical_page_id) * PAGE_SIZE;
  // set write cursor to offset
  db_io_.seekp(offset);
  db_io_.write(page_data, PAGE_SIZE);
  // check for I/O error
  if (db_io_.bad()) {
    LOG(ERROR) << "I/O error while writing";
    return;
  }
  // needs to flush to keep disk file in sync
  db_io_.flush();
}