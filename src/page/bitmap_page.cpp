#include "page/bitmap_page.h"

#include "glog/logging.h"

/**
 * TODO: Student Implement
 */
template <size_t PageSize>
bool BitmapPage<PageSize>::AllocatePage(uint32_t &page_offset) {  // 分配一个空闲页
  if (page_allocated_ >= 8 * MAX_CHARS) {
    return false;
  }
  uint32_t start_free_page = next_free_page_;
  for (uint32_t i = 0; i < MAX_CHARS * 8; i++) {
    uint32_t current_offset = (start_free_page + i) % (MAX_CHARS * 8);
    uint32_t byte_index = current_offset / 8;
    uint32_t bit_index = current_offset % 8;
    if (IsPageFreeLow(byte_index, bit_index)) {
      bytes[byte_index] |= 1 << bit_index;                       // 设置位
      ++page_allocated_;                                         // 分配页数加1
      next_free_page_ = (current_offset + 1) % (MAX_CHARS * 8);  // 更新下一个空闲页
      page_offset = current_offset;
      return true;
    }
  }
  return false;
}

/**
 * TODO: Student Implement
 */
template <size_t PageSize>
bool BitmapPage<PageSize>::DeAllocatePage(uint32_t page_offset) {
  if (page_offset >= 8 * MAX_CHARS || IsPageFree(page_offset)) {  // 合法性检查
    return false;
  }
  uint32_t byte_index = page_offset / 8;
  uint32_t bit_index = page_offset % 8;
  bytes[byte_index] &= ~(1 << bit_index);
  --page_allocated_;              // 释放页数减1
  next_free_page_ = page_offset;  // 更新下一个空闲页
  return true;
}

/**
 * TODO: Student Implement
 */
template <size_t PageSize>
bool BitmapPage<PageSize>::IsPageFree(uint32_t page_offset) const {  // 只进行边界检查，委托处理
  if (page_offset >= MAX_CHARS * 8) {
    return false;
  }
  uint32_t byte_index = page_offset / 8;
  uint32_t bit_index = page_offset % 8;
  return IsPageFreeLow(byte_index, bit_index);
}

template <size_t PageSize>
bool BitmapPage<PageSize>::IsPageFreeLow(uint32_t byte_index, uint8_t bit_index) const {  // 位运算判断页状态
  return (bytes[byte_index] & (1 << bit_index)) == 0;
}

template class BitmapPage<64>;

template class BitmapPage<128>;

template class BitmapPage<256>;

template class BitmapPage<512>;

template class BitmapPage<1024>;

template class BitmapPage<2048>;

template class BitmapPage<4096>;