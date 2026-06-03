#include "buffer/clock_replacer.h"

CLOCKReplacer::CLOCKReplacer(size_t num_pages) : capacity(num_pages) {}

CLOCKReplacer::~CLOCKReplacer() = default;

bool CLOCKReplacer::Victim(frame_id_t *frame_id) {
  if (clock_list.empty()) {
    return false;
  }

  while (true) {
    frame_id_t candidate = clock_list.front();
    clock_list.pop_front();
    clock_map.erase(candidate);

    if (clock_status[candidate] == 0) {
      *frame_id = candidate;
      clock_status.erase(candidate);
      return true;
    }
    // reference bit is 1, give a second chance
    clock_status[candidate] = 0;
    clock_list.push_back(candidate);
    clock_map[candidate] = std::prev(clock_list.end());
  }
}

void CLOCKReplacer::Pin(frame_id_t frame_id) {
  auto it = clock_status.find(frame_id);
  if (it != clock_status.end()) {
    auto list_it = clock_map.find(frame_id);
    clock_list.erase(list_it->second);
    clock_map.erase(list_it);
    clock_status.erase(it);
  }
}

void CLOCKReplacer::Unpin(frame_id_t frame_id) {
  auto it = clock_status.find(frame_id);
  if (it != clock_status.end()) {
    clock_status[frame_id] = 1;
  } else {
    clock_list.push_back(frame_id);
    clock_status[frame_id] = 1;
    clock_map[frame_id] = std::prev(clock_list.end());
  }
}

size_t CLOCKReplacer::Size() { return clock_status.size(); }
