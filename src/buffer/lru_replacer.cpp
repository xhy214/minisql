#include "buffer/lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages) : max_pages(num_pages) {}

LRUReplacer::~LRUReplacer() = default;

/**
 * TODO: Student Implement
 */
bool LRUReplacer::Victim(frame_id_t *frame_id) {  // 驱逐一个可用帧
  if (lru_list.empty()) {
    return false;
  }
  *frame_id = lru_list.back();
  lru_map.erase(*frame_id);
  lru_list.pop_back();
  return true;
}

/**
 * TODO: Student Implement
 */
void LRUReplacer::Pin(frame_id_t frame_id) {  // 固定一个帧
  auto it = lru_map.find(frame_id);
  if (it != lru_map.end()) {
    lru_list.erase(it->second);
    lru_map.erase(it);
  }
}

/**
 * TODO: Student Implement
 */
void LRUReplacer::Unpin(frame_id_t frame_id) {  // 取消固定一个帧
  auto it = lru_map.find(frame_id);
  if (it != lru_map.end()) {
    lru_list.erase(it->second);
    lru_map.erase(it);
  }
  lru_list.push_front(frame_id);
  lru_map[frame_id] = lru_list.begin();
}

/**
 * TODO: Student Implement
 */
size_t LRUReplacer::Size() { return lru_map.size(); }