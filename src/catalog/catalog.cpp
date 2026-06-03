#include "catalog/catalog.h"

void CatalogMeta::SerializeTo(char *buf) const {
  ASSERT(GetSerializedSize() <= PAGE_SIZE, "Failed to serialize catalog metadata to disk.");
  MACH_WRITE_UINT32(buf, CATALOG_METADATA_MAGIC_NUM);
  buf += 4;
  MACH_WRITE_UINT32(buf, table_meta_pages_.size());
  buf += 4;
  MACH_WRITE_UINT32(buf, index_meta_pages_.size());
  buf += 4;
  for (auto iter : table_meta_pages_) {
    MACH_WRITE_TO(table_id_t, buf, iter.first);
    buf += 4;
    MACH_WRITE_TO(page_id_t, buf, iter.second);
    buf += 4;
  }
  for (auto iter : index_meta_pages_) {
    MACH_WRITE_TO(index_id_t, buf, iter.first);
    buf += 4;
    MACH_WRITE_TO(page_id_t, buf, iter.second);
    buf += 4;
  }
}

CatalogMeta *CatalogMeta::DeserializeFrom(char *buf) {
  // check valid
  uint32_t magic_num = MACH_READ_UINT32(buf);
  buf += 4;
  ASSERT(magic_num == CATALOG_METADATA_MAGIC_NUM, "Failed to deserialize catalog metadata from disk.");
  // get table and index nums
  uint32_t table_nums = MACH_READ_UINT32(buf);
  buf += 4;
  uint32_t index_nums = MACH_READ_UINT32(buf);
  buf += 4;
  // create metadata and read value
  CatalogMeta *meta = new CatalogMeta();
  for (uint32_t i = 0; i < table_nums; i++) {
    auto table_id = MACH_READ_FROM(table_id_t, buf);
    buf += 4;
    auto table_heap_page_id = MACH_READ_FROM(page_id_t, buf);
    buf += 4;
    meta->table_meta_pages_.emplace(table_id, table_heap_page_id);
  }
  for (uint32_t i = 0; i < index_nums; i++) {
    auto index_id = MACH_READ_FROM(index_id_t, buf);
    buf += 4;
    auto index_page_id = MACH_READ_FROM(page_id_t, buf);
    buf += 4;
    meta->index_meta_pages_.emplace(index_id, index_page_id);
  }
  return meta;
}

/**
 * TODO: Student Implement
 */
uint32_t CatalogMeta::GetSerializedSize() const {  // 获取序列化大小
  return sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
         static_cast<uint32_t>(table_meta_pages_.size()) * (sizeof(table_id_t) + sizeof(page_id_t)) +
         static_cast<uint32_t>(index_meta_pages_.size()) * (sizeof(index_id_t) + sizeof(page_id_t));
}

CatalogMeta::CatalogMeta() {}

/**
 * TODO: Student Implement
 */
CatalogManager::CatalogManager(BufferPoolManager *buffer_pool_manager, LockManager *lock_manager,
                               LogManager *log_manager, bool init)
    : buffer_pool_manager_(buffer_pool_manager), lock_manager_(lock_manager), log_manager_(log_manager) {
  if (init) {  // 初始化，使用新的catalog元数据
    catalog_meta_ = CatalogMeta::NewInstance();
    next_table_id_ = 0;
    next_index_id_ = 0;
  } else {  // 恢复，加载现有的catalog元数据
    Page *page = buffer_pool_manager_->FetchPage(CATALOG_META_PAGE_ID);
    ASSERT(page != nullptr, "Failed to fetch catalog meta page");
    catalog_meta_ = CatalogMeta::DeserializeFrom(page->GetData());
    buffer_pool_manager_->UnpinPage(CATALOG_META_PAGE_ID, false);
    // 加载所有的表
    for (const auto &entry : catalog_meta_->table_meta_pages_) {
      table_id_t table_id = entry.first;
      page_id_t page_id = entry.second;
      LoadTable(table_id, page_id);
    }
    // 加载所有的索引
    for (const auto &entry : catalog_meta_->index_meta_pages_) {
      index_id_t index_id = entry.first;
      page_id_t page_id = entry.second;
      LoadIndex(index_id, page_id);
    }
    next_table_id_ = catalog_meta_->GetNextTableId();
    next_index_id_ = catalog_meta_->GetNextIndexId();
  }
}

CatalogManager::~CatalogManager() {
  FlushCatalogMetaPage();
  delete catalog_meta_;
  for (auto iter : tables_) {
    delete iter.second;
  }
  for (auto iter : indexes_) {
    delete iter.second;
  }
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::CreateTable(const string &table_name, TableSchema *schema, Txn *txn, TableInfo *&table_info) {
  if (table_names_.find(table_name) != table_names_.end()) {  // 查重
    return DB_TABLE_ALREADY_EXIST;
  }
  // 此处使用深拷贝防止并发问题
  TableSchema *copied_schema = Schema::DeepCopySchema(schema);
  TableHeap *table_heap = TableHeap::Create(buffer_pool_manager_, copied_schema, txn, log_manager_, lock_manager_);
  page_id_t root_page_id = table_heap->GetFirstPageId();
  table_id_t table_id = next_table_id_++;
  TableMetadata *table_meta = TableMetadata::Create(table_id, table_name, root_page_id, copied_schema);
  page_id_t meta_page_id;
  Page *meta_page = buffer_pool_manager_->NewPage(meta_page_id);
  if (meta_page == nullptr) {  // 分配失败，回滚操作
    next_table_id_--;
    delete table_meta;
    table_heap->DeleteTable(root_page_id);
    delete table_heap;
    return DB_FAILED;
  }
  table_meta->SerializeTo(meta_page->GetData());  // 序列化元数据
  buffer_pool_manager_->UnpinPage(meta_page_id, true);
  buffer_pool_manager_->FlushPage(meta_page_id);  // 5-13 Debug 刷盘持久化
  table_info = TableInfo::Create();
  table_info->Init(table_meta, table_heap);
  // 建立映射关系
  tables_[table_id] = table_info;
  table_names_[table_name] = table_id;
  catalog_meta_->table_meta_pages_[table_id] = meta_page_id;
  FlushCatalogMetaPage();
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTable(const string &table_name, TableInfo *&table_info) {
  auto it = table_names_.find(table_name);
  if (it == table_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  return GetTable(it->second, table_info);  // 委托查找
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTables(vector<TableInfo *> &tables) const {
  tables.reserve(tables.size() + tables_.size());
  for (const auto &iter : tables_) {  // 获取所有table信息
    tables.push_back(iter.second);
  }
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::CreateIndex(const std::string &table_name, const string &index_name,
                                    const std::vector<std::string> &index_keys, Txn *txn, IndexInfo *&index_info,
                                    const string &index_type) {
  TableInfo *table_info = nullptr;
  dberr_t ret = GetTable(table_name, table_info);
  if (ret != DB_SUCCESS) {
    return ret;
  }
  Schema *table_schema = table_info->GetSchema();
  std::vector<uint32_t> key_map;
  for (const auto &key_name : index_keys) {
    uint32_t col_idx;
    if (table_schema->GetColumnIndex(key_name, col_idx) != DB_SUCCESS) {
      return DB_COLUMN_NAME_NOT_EXIST;
    }
    key_map.push_back(col_idx);
  }
  // 索引查重
  auto table_it = index_names_.find(table_name);
  if (table_it != index_names_.end() && table_it->second.find(index_name) != table_it->second.end()) {
    return DB_INDEX_ALREADY_EXIST;
  }
  // 创建索引结构
  index_id_t index_id = next_index_id_++;
  IndexMetadata *index_meta = IndexMetadata::Create(index_id, index_name, table_info->GetTableId(), key_map);
  index_info = IndexInfo::Create();
  if (index_info == nullptr) {  // 创建失败，回滚
    delete index_meta;
    return DB_FAILED;
  }
  index_info->Init(index_meta, table_info, buffer_pool_manager_);
  if (index_info->GetIndex() == nullptr) {  // 创建失败，回滚
    delete index_info;
    return DB_FAILED;
  }
  // 5-13 Debug 全表扫描填充已有数据
  Index *index = index_info->GetIndex();
  IndexSchema *key_schema = index_info->GetIndexKeySchema();
  for (auto it = table_info->GetTableHeap()->Begin(txn); it != table_info->GetTableHeap()->End(); ++it) {
    Row key_row;
    it->GetKeyFromRow(table_schema, key_schema, key_row);
    index->InsertEntry(key_row, it->GetRowId(), txn);
  }
  page_id_t meta_page_id;
  Page *meta_page = buffer_pool_manager_->NewPage(meta_page_id);
  if (meta_page == nullptr) {  // 创建失败，回滚
    delete index_info;
    return DB_FAILED;
  }
  // 5-13 Debug 序列化元数据并刷盘
  index_meta->SerializeTo(meta_page->GetData());
  buffer_pool_manager_->UnpinPage(meta_page_id, true);
  buffer_pool_manager_->FlushPage(meta_page_id);
  // 建立映射
  indexes_[index_id] = index_info;
  index_names_[table_name][index_name] = index_id;
  catalog_meta_->index_meta_pages_[index_id] = meta_page_id;
  FlushCatalogMetaPage();
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetIndex(const std::string &table_name, const std::string &index_name,
                                 IndexInfo *&index_info) const {  // 查找索引
  auto table_it = index_names_.find(table_name);
  if (table_it == index_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  auto index_it = table_it->second.find(index_name);
  if (index_it == table_it->second.end()) {
    return DB_INDEX_NOT_FOUND;
  }
  index_info = indexes_.at(index_it->second);
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTableIndexes(const std::string &table_name,
                                        std::vector<IndexInfo *> &indexes) const {  // 查找表中所有索引
  if (table_names_.find(table_name) == table_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  auto table_it = index_names_.find(table_name);
  if (table_it == index_names_.end()) {
    return DB_SUCCESS;
  }
  indexes.reserve(indexes.size() + table_it->second.size());
  for (const auto &iter : table_it->second) {
    indexes.push_back(indexes_.at(iter.second));
  }
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::DropTable(const string &table_name) {
  auto it = table_names_.find(table_name);
  if (it == table_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  return DropTable(it->second);  // 委托删除
}

dberr_t CatalogManager::DropTable(table_id_t table_id) {
  TableInfo *table_info = nullptr;
  dberr_t ret = GetTable(table_id, table_info);
  if (ret != DB_SUCCESS) {
    return ret;
  }
  // 收集该表下所有索引，逐个删除
  std::vector<index_id_t> index_ids;
  auto index_it = index_names_.find(table_info->GetTableName());
  if (index_it != index_names_.end()) {
    for (const auto &entry : index_it->second) {
      index_ids.push_back(entry.second);
    }
  }
  for (auto index_id : index_ids) {
    delete indexes_[index_id];
    indexes_.erase(index_id);
    catalog_meta_->DeleteIndexMetaPage(buffer_pool_manager_, index_id);
  }
  index_names_.erase(table_info->GetTableName());
  // 回收页面
  table_info->GetTableHeap()->FreeTableHeap();
  // 删除元数据页
  page_id_t meta_page_id = catalog_meta_->table_meta_pages_[table_id];
  buffer_pool_manager_->DeletePage(meta_page_id);
  // 清理映射并刷盘
  catalog_meta_->table_meta_pages_.erase(table_id);
  table_names_.erase(table_info->GetTableName());
  tables_.erase(table_id);
  delete table_info;
  FlushCatalogMetaPage();
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::DropIndex(const string &table_name, const string &index_name) {
  auto table_it = index_names_.find(table_name);
  if (table_it == index_names_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  auto index_it = table_it->second.find(index_name);
  if (index_it == table_it->second.end()) {
    return DB_INDEX_NOT_FOUND;
  }
  index_id_t index_id = index_it->second;  // 二次跳转定位索引
  delete indexes_[index_id];
  indexes_.erase(index_id);
  table_it->second.erase(index_it);
  if (table_it->second.empty()) {
    index_names_.erase(table_it);
  }
  // 删除元数据页并持久化
  catalog_meta_->DeleteIndexMetaPage(buffer_pool_manager_, index_id);
  FlushCatalogMetaPage();
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::FlushCatalogMetaPage() const {  // 辅助刷盘
  Page *page = buffer_pool_manager_->FetchPage(CATALOG_META_PAGE_ID);
  if (page == nullptr) {
    return DB_FAILED;
  }
  catalog_meta_->SerializeTo(page->GetData());
  buffer_pool_manager_->UnpinPage(CATALOG_META_PAGE_ID, true);
  buffer_pool_manager_->FlushPage(CATALOG_META_PAGE_ID);
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::LoadTable(const table_id_t table_id, const page_id_t page_id) {
  // 获取页面并反序列化
  Page *page = buffer_pool_manager_->FetchPage(page_id);
  if (page == nullptr) {
    return DB_FAILED;
  }
  TableMetadata *table_meta = nullptr;
  TableMetadata::DeserializeFrom(page->GetData(), table_meta);
  if (table_meta == nullptr) {
    buffer_pool_manager_->UnpinPage(page_id, false);
    return DB_FAILED;
  }
  TableInfo *table_info = TableInfo::Create();
  if (table_info == nullptr) {
    delete table_meta;
    buffer_pool_manager_->UnpinPage(page_id, false);
    return DB_FAILED;
  }
  TableHeap *table_heap = TableHeap::Create(buffer_pool_manager_, table_meta->GetFirstPageId(), table_meta->GetSchema(),
                                            log_manager_, lock_manager_);
  table_info->Init(table_meta, table_heap);
  // 建立映射
  tables_[table_id] = table_info;
  table_names_[table_meta->GetTableName()] = table_id;
  buffer_pool_manager_->UnpinPage(page_id, false);
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::LoadIndex(const index_id_t index_id, const page_id_t page_id) {
  // 拿到指定页面并反序列化
  Page *page = buffer_pool_manager_->FetchPage(page_id);
  if (page == nullptr) {
    return DB_FAILED;
  }
  IndexMetadata *index_meta = nullptr;
  IndexMetadata::DeserializeFrom(page->GetData(), index_meta);
  if (index_meta == nullptr) {  // 失败回滚
    buffer_pool_manager_->UnpinPage(page_id, false);
    return DB_FAILED;
  }
  TableInfo *table_info = nullptr;
  dberr_t ret = GetTable(index_meta->GetTableId(), table_info);
  if (ret != DB_SUCCESS) {  // 失败回滚
    delete index_meta;
    buffer_pool_manager_->UnpinPage(page_id, false);
    return ret;
  }
  IndexInfo *index_info = IndexInfo::Create();
  if (index_info == nullptr) {
    delete index_meta;
    buffer_pool_manager_->UnpinPage(page_id, false);
    return DB_FAILED;
  }
  index_info->Init(index_meta, table_info, buffer_pool_manager_);
  // 建立映射
  indexes_[index_id] = index_info;
  index_names_[table_info->GetTableName()][index_meta->GetIndexName()] = index_id;
  buffer_pool_manager_->UnpinPage(page_id, false);
  return DB_SUCCESS;
}

/**
 * TODO: Student Implement
 */
dberr_t CatalogManager::GetTable(const table_id_t table_id, TableInfo *&table_info) {  // 底层查找表
  auto it = tables_.find(table_id);
  if (it == tables_.end()) {
    return DB_TABLE_NOT_EXIST;
  }
  table_info = it->second;
  return DB_SUCCESS;
}