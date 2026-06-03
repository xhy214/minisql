#include "record/column.h"

#include "glog/logging.h"

Column::Column(std::string column_name, TypeId type, uint32_t index, bool nullable, bool unique)
    : name_(std::move(column_name)), type_(type), table_ind_(index), nullable_(nullable), unique_(unique) {
  ASSERT(type != TypeId::kTypeChar, "Wrong constructor for CHAR type.");
  switch (type) {
    case TypeId::kTypeInt:
      len_ = sizeof(int32_t);
      break;
    case TypeId::kTypeFloat:
      len_ = sizeof(float_t);
      break;
    default:
      ASSERT(false, "Unsupported column type.");
  }
}

Column::Column(std::string column_name, TypeId type, uint32_t length, uint32_t index, bool nullable, bool unique)
    : name_(std::move(column_name)),
      type_(type),
      len_(length),
      table_ind_(index),
      nullable_(nullable),
      unique_(unique) {
  ASSERT(type == TypeId::kTypeChar, "Wrong constructor for non-VARCHAR type.");
}

Column::Column(const Column *other)
    : name_(other->name_),
      type_(other->type_),
      len_(other->len_),
      table_ind_(other->table_ind_),
      nullable_(other->nullable_),
      unique_(other->unique_) {}

/**
 * TODO: Student Implement
 */
uint32_t Column::SerializeTo(char *buf) const {  // 将列定义序列化到缓冲
  uint32_t offset = 0;
  memcpy(buf + offset, &COLUMN_MAGIC_NUM, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  uint32_t name_len = name_.length();
  memcpy(buf + offset, &name_len, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(buf + offset, name_.c_str(), name_len);
  offset += name_len;
  memcpy(buf + offset, &type_, sizeof(TypeId));
  offset += sizeof(TypeId);
  memcpy(buf + offset, &len_, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(buf + offset, &table_ind_, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(buf + offset, &nullable_, sizeof(bool));
  offset += sizeof(bool);
  memcpy(buf + offset, &unique_, sizeof(bool));
  offset += sizeof(bool);
  return offset;
}

/**
 * TODO: Student Implement
 */
uint32_t Column::GetSerializedSize() const {  // 获取序列化大小
  uint32_t total_size = 4 * sizeof(uint32_t) + name_.length() + sizeof(TypeId) + 2 * sizeof(bool);
  return total_size;
}

/**
 * TODO: Student Implement
 */
uint32_t Column::DeserializeFrom(char *buf, Column *&column) {  // 从缓冲反序列化
  uint32_t offset = 0, name_len = 0, magic_num;
  memcpy(&magic_num, buf + offset, sizeof(uint32_t));
  if (magic_num != Column::COLUMN_MAGIC_NUM) {//魔数校验失败，直接返回0
    return 0;
  }
  column = new Column();
  offset += sizeof(uint32_t);
  memcpy(&name_len, buf + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  char *name = new char[name_len + 1];
  memcpy(name, buf + offset, name_len);
  name[name_len] = '\0';
  column->name_ = std::string(name);
  delete[] name;
  offset += name_len;
  memcpy(&column->type_, buf + offset, sizeof(TypeId));
  offset += sizeof(TypeId);
  memcpy(&column->len_, buf + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(&column->table_ind_, buf + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(&column->nullable_, buf + offset, sizeof(bool));
  offset += sizeof(bool);
  memcpy(&column->unique_, buf + offset, sizeof(bool));
  offset += sizeof(bool);
  return offset;
}
