#ifndef MINISQL_ROW_H
#define MINISQL_ROW_H

#include <bitset>
#include <memory>
#include <vector>
#include "common/macros.h"
#include "common/rowid.h"
#include "record/field.h"
#include "record/schema.h"
/*
This is my helper class to store bitmap for null fields
 */
class Bitmap {
  std::vector<uint8_t> bits;

 public:
  Bitmap() = default;
  Bitmap(uint32_t size) {  // size is the number of fields
    ASSERT(size > 0, "Row must have at least one field.");
    bits.resize((size + 7) >> 3, 0);
  }
  Bitmap(const uint8_t *raw_bits, uint32_t byte_size) {
    ASSERT(raw_bits != nullptr, "Raw bits cannot be null.");
    bits.assign(raw_bits, raw_bits + byte_size);
  }
  void Set(uint32_t index) { bits[index >> 3] |= (1 << (index & 7)); }
  bool IsNull(uint32_t index) const { return !((bits[index >> 3] >> (index & 7)) & 1); }
  const uint8_t *GetData() const { return bits.data(); }
  const uint32_t GetSize() const { return bits.size(); }
};
/**
 *  Row format:
 * -------------------------------------------
 * | Header | Field-1 | ... | Field-N |
 * -------------------------------------------
 *  Header format:
 * --------------------------------------------
 * | Field Nums | Null bitmap |
 * -------------------------------------------
 *
 *
 */
class Row {
 public:
  /**
   * Row used for insert
   * Field integrity should check by upper level
   */
  Row(std::vector<Field> &fields) {
    // deep copy
    for (auto &field : fields) {
      fields_.push_back(new Field(field));
    }
  }

  void destroy() {
    if (!fields_.empty()) {
      for (auto field : fields_) {
        delete field;
      }
      fields_.clear();
    }
  }

  ~Row() { destroy(); };

  /**
   * Row used for deserialize
   */
  Row() = default;

  /**
   * Row used for deserialize and update
   */
  Row(RowId rid) : rid_(rid) {}

  /**
   * Row copy function, deep copy
   */
  Row(const Row &other) {
    destroy();
    rid_ = other.rid_;
    for (auto &field : other.fields_) {
      fields_.push_back(new Field(*field));
    }
  }

  /**
   * Assign operator, deep copy
   */
  Row &operator=(const Row &other) {
    destroy();
    rid_ = other.rid_;
    for (auto &field : other.fields_) {
      fields_.push_back(new Field(*field));
    }
    return *this;
  }

  /**
   * Note: Make sure that bytes write to buf is equal to GetSerializedSize()
   */
  uint32_t SerializeTo(char *buf, Schema *schema) const;

  uint32_t DeserializeFrom(char *buf, Schema *schema);

  /**
   * For empty row, return 0
   * For non-empty row with null fields, eg: |null|null|null|, return header size only
   * @return
   */
  uint32_t GetSerializedSize(Schema *schema) const;

  void GetKeyFromRow(const Schema *schema, const Schema *key_schema, Row &key_row);

  inline const RowId GetRowId() const { return rid_; }

  inline void SetRowId(RowId rid) { rid_ = rid; }

  inline std::vector<Field *> &GetFields() { return fields_; }

  inline Field *GetField(uint32_t idx) const {
    ASSERT(idx < fields_.size(), "Failed to access field");
    return fields_[idx];
  }

  inline size_t GetFieldCount() const { return fields_.size(); }

 private:
  RowId rid_{};
  std::vector<Field *> fields_; /** Make sure that all field ptr are destructed*/
};

#endif  // MINISQL_ROW_H
