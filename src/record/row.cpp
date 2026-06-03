#include "record/row.h"

/**
 * TODO: Student Implement
 */
uint32_t Row::SerializeTo(char *buf, Schema *schema) const {  // 序列化到缓冲
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(schema->GetColumnCount() == fields_.size(), "Fields size do not match schema's column size.");
  uint32_t offset = 0, field_size = schema->GetColumnCount();
  memcpy(buf + offset, &field_size, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  Bitmap bitmap(field_size);
  uint32_t reverse_bitmap_offset = offset;  // 提前预留位图，跳过继续读取
  offset += bitmap.GetSize();
  for (uint32_t i = 0; i < field_size; ++i) {
    if (fields_[i]->IsNull()) {
    } else {
      bitmap.Set(i);
      offset += fields_[i]->SerializeTo(buf + offset);
    }
  }
  memcpy(buf + reverse_bitmap_offset, bitmap.GetData(), bitmap.GetSize());
  return offset;
}

uint32_t Row::DeserializeFrom(char *buf, Schema *schema) {  // 从缓冲反序列化
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(fields_.empty(), "Non empty field in row.");
  fields_ = std::vector<Field *>();
  rid_ = INVALID_ROWID;
  uint32_t offset = 0, field_size = 0;
  memcpy(&field_size, buf + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  uint32_t byte_size = (field_size + 7) >> 3;
  Bitmap bitmap(reinterpret_cast<const uint8_t *>(buf + offset), byte_size);
  offset += byte_size;
  fields_.reserve(field_size);
  for (uint32_t i = 0; i < field_size; ++i) {
    TypeId type = schema->GetColumn(i)->GetType();
    Field *field = nullptr;
    bool is_null = bitmap.IsNull(i);
    offset += Field::DeserializeFrom(buf + offset, type, &field, is_null);
    fields_.emplace_back(field);
  }
  return offset;
}

uint32_t Row::GetSerializedSize(Schema *schema) const {  // 计算缓冲大小
  ASSERT(schema != nullptr, "Invalid schema before serialize.");
  ASSERT(schema->GetColumnCount() == fields_.size(), "Fields size do not match schema's column size.");
  uint32_t total_size = sizeof(uint32_t);
  total_size += (fields_.size() + 7) >> 3;
  for (const auto &field : fields_) {
    if (!field->IsNull()) {
      total_size += field->GetSerializedSize();
    }
  }
  return total_size;
}

void Row::GetKeyFromRow(const Schema *schema, const Schema *key_schema, Row &key_row) {  // 构造只含索引属性的行
  auto columns = key_schema->GetColumns();
  std::vector<Field> fields;
  uint32_t idx;
  for (auto column : columns) {
    schema->GetColumnIndex(column->GetName(), idx);
    fields.emplace_back(*this->GetField(idx));
  }
  key_row = Row(fields);
}
