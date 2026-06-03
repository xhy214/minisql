#include "record/schema.h"

/**
 * TODO: Student Implement
 */
uint32_t Schema::SerializeTo(char *buf) const {  // 序列化到缓冲
  uint32_t offset = 0;
  memcpy(buf, &SCHEMA_MAGIC_NUM, sizeof(SCHEMA_MAGIC_NUM));
  offset += sizeof(SCHEMA_MAGIC_NUM);
  uint32_t column_count = columns_.size();
  memcpy(buf + offset, &column_count, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  for (const auto &column : columns_) {
    offset += column->SerializeTo(buf + offset);
  }
  memcpy(buf + offset, &is_manage_, sizeof(bool));
  offset += sizeof(bool);
  return offset;
}

uint32_t Schema::GetSerializedSize() const {  // 得到序列化大小
  uint32_t total_size = 2 * sizeof(uint32_t) + sizeof(bool);
  for (const auto &column : columns_) {
    total_size += column->GetSerializedSize();
  }
  return total_size;
}

uint32_t Schema::DeserializeFrom(char *buf, Schema *&schema) {  // 从缓冲反序列化
  uint32_t offset = 0, column_count = 0, magic_num;
  memcpy(&magic_num, buf + offset, sizeof(uint32_t));
  if (magic_num != Schema::SCHEMA_MAGIC_NUM) {  // 魔数错误
    return 0;
  }
  schema = new Schema();
  offset += sizeof(uint32_t);
  memcpy(&column_count, buf + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  schema->columns_.reserve(column_count);
  for (uint32_t i = 0; i < column_count; ++i) {
    Column *col = nullptr;
    offset += Column::DeserializeFrom(buf + offset, col);
    schema->columns_.emplace_back(std::move(col));
  }
  memcpy(&schema->is_manage_, buf + offset, sizeof(bool));
  offset += sizeof(bool);
  return offset;
}