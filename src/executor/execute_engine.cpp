#include "executor/execute_engine.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <chrono>

#include "common/result_writer.h"
#include "executor/executors/delete_executor.h"
#include "executor/executors/index_scan_executor.h"
#include "executor/executors/insert_executor.h"
#include "executor/executors/seq_scan_executor.h"
#include "executor/executors/update_executor.h"
#include "executor/executors/values_executor.h"
#include "glog/logging.h"
#include "planner/planner.h"
#include "utils/utils.h"

ExecuteEngine::ExecuteEngine() {
  char path[] = "./databases";
  DIR *dir;
  if ((dir = opendir(path)) == nullptr) {
    mkdir("./databases", 0777);
    dir = opendir(path);
  }

  struct dirent *stdir;
  while ((stdir = readdir(dir)) != nullptr) {
    if (strcmp(stdir->d_name, ".") == 0 || strcmp(stdir->d_name, "..") == 0 || stdir->d_name[0] == '.') continue;
    dbs_[stdir->d_name] = new DBStorageEngine(stdir->d_name, false);
  }

  closedir(dir);
}

std::unique_ptr<AbstractExecutor> ExecuteEngine::CreateExecutor(ExecuteContext *exec_ctx,
                                                                const AbstractPlanNodeRef &plan) {
  switch (plan->GetType()) {
    // Create a new sequential scan executor
    case PlanType::SeqScan: {
      return std::make_unique<SeqScanExecutor>(exec_ctx, dynamic_cast<const SeqScanPlanNode *>(plan.get()));
    }
    // Create a new index scan executor
    case PlanType::IndexScan: {
      return std::make_unique<IndexScanExecutor>(exec_ctx, dynamic_cast<const IndexScanPlanNode *>(plan.get()));
    }
    // Create a new update executor
    case PlanType::Update: {
      auto update_plan = dynamic_cast<const UpdatePlanNode *>(plan.get());
      auto child_executor = CreateExecutor(exec_ctx, update_plan->GetChildPlan());
      return std::make_unique<UpdateExecutor>(exec_ctx, update_plan, std::move(child_executor));
    }
      // Create a new delete executor
    case PlanType::Delete: {
      auto delete_plan = dynamic_cast<const DeletePlanNode *>(plan.get());
      auto child_executor = CreateExecutor(exec_ctx, delete_plan->GetChildPlan());
      return std::make_unique<DeleteExecutor>(exec_ctx, delete_plan, std::move(child_executor));
    }
    case PlanType::Insert: {
      auto insert_plan = dynamic_cast<const InsertPlanNode *>(plan.get());
      auto child_executor = CreateExecutor(exec_ctx, insert_plan->GetChildPlan());
      return std::make_unique<InsertExecutor>(exec_ctx, insert_plan, std::move(child_executor));
    }
    case PlanType::Values: {
      return std::make_unique<ValuesExecutor>(exec_ctx, dynamic_cast<const ValuesPlanNode *>(plan.get()));
    }
    default:
      throw std::logic_error("Unsupported plan type.");
  }
}

dberr_t ExecuteEngine::ExecutePlan(const AbstractPlanNodeRef &plan, std::vector<Row> *result_set, Txn *txn,
                                   ExecuteContext *exec_ctx) {
  // Construct the executor for the abstract plan node
  auto executor = CreateExecutor(exec_ctx, plan);

  try {
    executor->Init();
    RowId rid{};
    Row row{};
    while (executor->Next(&row, &rid)) {
      if (result_set != nullptr) {
        result_set->push_back(row);
      }
    }
  } catch (const exception &ex) {
    std::cout << "Error Encountered in Executor Execution: " << ex.what() << std::endl;
    if (result_set != nullptr) {
      result_set->clear();
    }
    return DB_FAILED;
  }
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::Execute(pSyntaxNode ast) {
  if (ast == nullptr) {
    return DB_FAILED;
  }
  auto start_time = std::chrono::system_clock::now();
  unique_ptr<ExecuteContext> context(nullptr);
  if (!current_db_.empty()) context = dbs_[current_db_]->MakeExecuteContext(nullptr);
  switch (ast->type_) {
    case kNodeCreateDB:
      return ExecuteCreateDatabase(ast, context.get());
    case kNodeDropDB:
      return ExecuteDropDatabase(ast, context.get());
    case kNodeShowDB:
      return ExecuteShowDatabases(ast, context.get());
    case kNodeUseDB:
      return ExecuteUseDatabase(ast, context.get());
    case kNodeShowTables:
      return ExecuteShowTables(ast, context.get());
    case kNodeCreateTable:
      return ExecuteCreateTable(ast, context.get());
    case kNodeDropTable:
      return ExecuteDropTable(ast, context.get());
    case kNodeShowIndexes:
      return ExecuteShowIndexes(ast, context.get());
    case kNodeCreateIndex:
      return ExecuteCreateIndex(ast, context.get());
    case kNodeDropIndex:
      return ExecuteDropIndex(ast, context.get());
    case kNodeTrxBegin:
      return ExecuteTrxBegin(ast, context.get());
    case kNodeTrxCommit:
      return ExecuteTrxCommit(ast, context.get());
    case kNodeTrxRollback:
      return ExecuteTrxRollback(ast, context.get());
    case kNodeExecFile:
      return ExecuteExecfile(ast, context.get());
    case kNodeQuit:
      return ExecuteQuit(ast, context.get());
    default:
      break;
  }
  // Plan the query.
  Planner planner(context.get());
  std::vector<Row> result_set{};
  try {
    planner.PlanQuery(ast);
    // Execute the query.
    ExecutePlan(planner.plan_, &result_set, nullptr, context.get());
  } catch (const exception &ex) {
    std::cout << "Error Encountered in Planner: " << ex.what() << std::endl;
    return DB_FAILED;
  }
  auto stop_time = std::chrono::system_clock::now();
  double duration_time =
      double((std::chrono::duration_cast<std::chrono::milliseconds>(stop_time - start_time)).count());
  // Return the result set as string.
  std::stringstream ss;
  ResultWriter writer(ss);

  if (planner.plan_->GetType() == PlanType::SeqScan || planner.plan_->GetType() == PlanType::IndexScan) {
    auto schema = planner.plan_->OutputSchema();
    auto num_of_columns = schema->GetColumnCount();
    if (!result_set.empty()) {
      // find the max width for each column
      vector<int> data_width(num_of_columns, 0);
      for (const auto &row : result_set) {
        for (uint32_t i = 0; i < num_of_columns; i++) {
          data_width[i] = max(data_width[i], int(row.GetField(i)->toString().size()));
        }
      }
      int k = 0;
      for (const auto &column : schema->GetColumns()) {
        data_width[k] = max(data_width[k], int(column->GetName().length()));
        k++;
      }
      // Generate header for the result set.
      writer.Divider(data_width);
      k = 0;
      writer.BeginRow();
      for (const auto &column : schema->GetColumns()) {
        writer.WriteHeaderCell(column->GetName(), data_width[k++]);
      }
      writer.EndRow();
      writer.Divider(data_width);

      // Transforming result set into strings.
      for (const auto &row : result_set) {
        writer.BeginRow();
        for (uint32_t i = 0; i < schema->GetColumnCount(); i++) {
          writer.WriteCell(row.GetField(i)->toString(), data_width[i]);
        }
        writer.EndRow();
      }
      writer.Divider(data_width);
    }
    writer.EndInformation(result_set.size(), duration_time, true);
  } else {
    writer.EndInformation(result_set.size(), duration_time, false);
  }
  std::cout << writer.stream_.rdbuf();
  // todo:: use shared_ptr for schema
  if (ast->type_ == kNodeSelect) delete planner.plan_->OutputSchema();
  return DB_SUCCESS;
}

void ExecuteEngine::ExecuteInformation(dberr_t result) {
  switch (result) {
    case DB_ALREADY_EXIST:
      cout << "Database already exists." << endl;
      break;
    case DB_NOT_EXIST:
      cout << "Database not exists." << endl;
      break;
    case DB_TABLE_ALREADY_EXIST:
      cout << "Table already exists." << endl;
      break;
    case DB_TABLE_NOT_EXIST:
      cout << "Table not exists." << endl;
      break;
    case DB_INDEX_ALREADY_EXIST:
      cout << "Index already exists." << endl;
      break;
    case DB_INDEX_NOT_FOUND:
      cout << "Index not exists." << endl;
      break;
    case DB_COLUMN_NAME_NOT_EXIST:
      cout << "Column not exists." << endl;
      break;
    case DB_KEY_NOT_FOUND:
      cout << "Key not exists." << endl;
      break;
    case DB_QUIT:
      cout << "Bye." << endl;
      break;
    default:
      break;
  }
}

dberr_t ExecuteEngine::ExecuteCreateDatabase(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteCreateDatabase" << std::endl;
#endif
  string db_name = ast->child_->val_;
  if (dbs_.find(db_name) != dbs_.end()) {
    return DB_ALREADY_EXIST;
  }
  dbs_.insert(make_pair(db_name, new DBStorageEngine(db_name, true)));
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::ExecuteDropDatabase(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteDropDatabase" << std::endl;
#endif
  string db_name = ast->child_->val_;
  if (dbs_.find(db_name) == dbs_.end()) {
    return DB_NOT_EXIST;
  }
  remove(("./databases/" + db_name).c_str());
  delete dbs_[db_name];
  dbs_.erase(db_name);
  if (db_name == current_db_) current_db_ = "";
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::ExecuteShowDatabases(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteShowDatabases" << std::endl;
#endif
  if (dbs_.empty()) {
    cout << "Empty set (0.00 sec)" << endl;
    return DB_SUCCESS;
  }
  int max_width = 8;
  for (const auto &itr : dbs_) {
    if (itr.first.length() > max_width) max_width = itr.first.length();
  }
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  cout << "| " << std::left << setfill(' ') << setw(max_width) << "Database"
       << " |" << endl;
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  for (const auto &itr : dbs_) {
    cout << "| " << std::left << setfill(' ') << setw(max_width) << itr.first << " |" << endl;
  }
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  return DB_SUCCESS;
}

dberr_t ExecuteEngine::ExecuteUseDatabase(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteUseDatabase" << std::endl;
#endif
  string db_name = ast->child_->val_;
  if (dbs_.find(db_name) != dbs_.end()) {
    current_db_ = db_name;
    cout << "Database changed" << endl;
    return DB_SUCCESS;
  }
  return DB_NOT_EXIST;
}

dberr_t ExecuteEngine::ExecuteShowTables(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteShowTables" << std::endl;
#endif
  if (current_db_.empty()) {
    cout << "No database selected" << endl;
    return DB_FAILED;
  }
  vector<TableInfo *> tables;
  if (dbs_[current_db_]->catalog_mgr_->GetTables(tables) == DB_FAILED) {
    cout << "Empty set (0.00 sec)" << endl;
    return DB_FAILED;
  }
  string table_in_db("Tables_in_" + current_db_);
  uint max_width = table_in_db.length();
  for (const auto &itr : tables) {
    if (itr->GetTableName().length() > max_width) max_width = itr->GetTableName().length();
  }
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  cout << "| " << std::left << setfill(' ') << setw(max_width) << table_in_db << " |" << endl;
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  for (const auto &itr : tables) {
    cout << "| " << std::left << setfill(' ') << setw(max_width) << itr->GetTableName() << " |" << endl;
  }
  cout << "+" << setfill('-') << setw(max_width + 2) << ""
       << "+" << endl;
  return DB_SUCCESS;
}
/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteCreateTable(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteCreateTable" << std::endl;
#endif
  if (current_db_.empty() || context == nullptr) {
    return DB_FAILED;
  }
  string table_name = ast->child_->val_;
  TableInfo *table_info = nullptr;
  if (context->GetCatalog()->GetTable(table_name, table_info) == DB_SUCCESS) {
    return DB_TABLE_ALREADY_EXIST;
  }
  pSyntaxNode col_definition = ast->child_->next_;
  vector<Column *> columns;
  // 第一次迭代仅收集主键节点
  unordered_set<string> primary_keys;
  pSyntaxNode node = col_definition->child_;
  while (node != nullptr) {
    if (node->type_ == kNodeColumnList && node->val_ != nullptr && string(node->val_) == "primary keys") {
      pSyntaxNode pm_col = node->child_;
      while (pm_col != nullptr) {
        primary_keys.insert(string(pm_col->val_));
        pm_col = pm_col->next_;
      }
    }
    node = node->next_;
  }
  // 第二次迭代收集所有列定义
  node = col_definition->child_;
  column_id_t col_idx = 0;
  while (node != nullptr) {
    if (node->type_ == kNodeColumnDefinition) {
      string col_name = node->child_->val_;
      pSyntaxNode type_node = node->child_->next_;
      string type_str = type_node->val_;
      TypeId type;
      uint32_t length = 0;
      if (type_str == "int") {
        type = kTypeInt;
      } else if (type_str == "float") {
        type = kTypeFloat;
      } else if (type_str == "char") {
        type = kTypeChar;
        length = stoi(type_node->child_->val_);
      }
      bool is_unique = (node->val_ != nullptr && string(node->val_) == "unique");//通过字段判断
      bool is_primary = (primary_keys.find(col_name) != primary_keys.end());//用第一次迭代的集合判断
      bool col_unique = is_unique || is_primary;
      bool col_nullable = !is_primary;//主键列不为空
      Column *col;
      if (type == kTypeChar) {
        col = new Column(col_name, type, length, col_idx, col_nullable, col_unique);
      } else {
        col = new Column(col_name, type, col_idx, col_nullable, col_unique);
      }
      columns.emplace_back(col);
      col_idx++;
    }
    node = node->next_;
  }
  TableSchema *schema = new TableSchema(columns);
  TableInfo *new_info = nullptr;
  dberr_t result = context->GetCatalog()->CreateTable(table_name, schema, nullptr, new_info);
  if (result != DB_SUCCESS) {
    delete schema;
    return DB_FAILED;
  }
  return DB_SUCCESS;
}
/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteDropTable(pSyntaxNode ast, ExecuteContext *context) {//委托删除表
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteDropTable" << std::endl;
#endif
  if (current_db_.empty() || context == nullptr) {
    return DB_FAILED;
  }
  return context->GetCatalog()->DropTable(ast->child_->val_);
}
/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteShowIndexes(pSyntaxNode ast, ExecuteContext *context) {//显示索引信息
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteShowIndexes" << std::endl;
#endif
  if (current_db_.empty() || context == nullptr) {
    return DB_FAILED;
  }
  vector<TableInfo *> tables;
  context->GetCatalog()->GetTables(tables);
  vector<tuple<string, string, string>> index_list;
  uint32_t max_table_width = 5;
  uint32_t max_index_width = 5;
  uint32_t max_key_width = 4;
  for (auto table : tables) {//获取每张表的索引信息
    vector<IndexInfo *> indexs;
    context->GetCatalog()->GetTableIndexes(table->GetTableName(), indexs);
    for (auto index : indexs) {
      string table_name = table->GetTableName();
      string index_name = index->GetIndexName();
      string key_cols;
      auto key_schema = index->GetIndexKeySchema();
      auto columns = key_schema->GetColumns();
      for (size_t i = 0; i < columns.size(); i++) {
        if (i != 0) key_cols += ", ";
        key_cols += columns[i]->GetName();
      }
      index_list.emplace_back(table_name, index_name, key_cols);
      //计算最大宽度便于对齐
      max_table_width = max(max_table_width, static_cast<uint32_t>(table_name.length()));
      max_index_width = max(max_index_width, static_cast<uint32_t>(index_name.length()));
      max_key_width = max(max_key_width, static_cast<uint32_t>(key_cols.length()));
    }
  }
  if (index_list.empty()) {
    cout << "Empty set" << endl;
    return DB_SUCCESS;
  }
  auto printer = [&]() {
    cout << "+" << setfill('-') << setw(max_table_width + 2) << ""
         << "+" << setfill('-') << setw(max_index_width + 2) << ""
         << "+" << setfill('-') << setw(max_key_width + 2) << ""
         << "+" << endl;
  };
  printer();
  cout << "| " << std::left << setfill(' ') << setw(max_table_width) << "Table"
       << " | " << setw(max_index_width) << "Index"
       << " | " << setw(max_key_width) << "Key"
       << " |" << endl;
  printer();
  for (const auto &item : index_list) {
    cout << "| " << std::left << setfill(' ') << setw(max_table_width) << get<0>(item) << " | " << setw(max_index_width)
         << get<1>(item) << " | " << setw(max_key_width) << get<2>(item) << " |" << endl;
  }
  printer();
  return DB_SUCCESS;
}
/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteCreateIndex(pSyntaxNode ast, ExecuteContext *context) {//解析索引元数据并委托构造
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteCreateIndex" << std::endl;
#endif
  if (current_db_.empty() || context == nullptr) {
    return DB_FAILED;
  }
  string index_name = ast->child_->val_;
  string table_name = ast->child_->next_->val_;
  pSyntaxNode col_list_node = ast->child_->next_->next_;
  vector<string> index_keys;
  pSyntaxNode col = col_list_node->child_;
  while (col != nullptr) {
    index_keys.emplace_back(string(col->val_));
    col = col->next_;
  }
  string index_type = "bptree";
  pSyntaxNode type_node = col_list_node->next_;
  if (type_node != nullptr && type_node->type_ == kNodeIndexType) {
    index_type = string(type_node->child_->val_);
  }
  IndexInfo *index_info = nullptr;
  return context->GetCatalog()->CreateIndex(table_name, index_name, index_keys, nullptr, index_info, index_type);
}
/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteDropIndex(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteDropIndex" << std::endl;
#endif
  if (current_db_.empty() || context == nullptr) {
    return DB_FAILED;
  }
  string index_name = ast->child_->val_;
  vector<TableInfo *> tables;
  context->GetCatalog()->GetTables(tables);
  for (auto table : tables) {
    IndexInfo *index_info = nullptr;
    if (context->GetCatalog()->GetIndex(table->GetTableName(), index_name, index_info) == DB_SUCCESS) {
      return context->GetCatalog()->DropIndex(table->GetTableName(), index_name);
    }
  }
  return DB_INDEX_NOT_FOUND;
}

dberr_t ExecuteEngine::ExecuteTrxBegin(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteTrxBegin" << std::endl;
#endif
  return DB_FAILED;
}

dberr_t ExecuteEngine::ExecuteTrxCommit(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteTrxCommit" << std::endl;
#endif
  return DB_FAILED;
}

dberr_t ExecuteEngine::ExecuteTrxRollback(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteTrxRollback" << std::endl;
#endif
  return DB_FAILED;
}
/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteExecfile(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteExecfile" << std::endl;
#endif
  string file_path = ast->child_->val_;
  //清洗路径，并一次读取整个文件到字符串
  if (file_path.size() >= 2 && file_path.front() == '"' && file_path.back() == '"') {
    file_path = file_path.substr(1, file_path.size() - 2);
  }
  ifstream infile(file_path);
  if (!infile.is_open()) {
    cout << "Failed to open file: " << file_path << endl;
    return DB_FAILED;
  }
  string content((istreambuf_iterator<char>(infile)), istreambuf_iterator<char>());
  infile.close();
  stringstream ss(content);
  string sql;
  auto total_start = std::chrono::system_clock::now();//记录执行时间
  int sql_count = 0;//记录执行的SQL语句数
  while (getline(ss, sql, ';')) {//使用流以分号读取SQL语句
    sql.erase(0, sql.find_first_not_of(" \t\n\r"));
    sql.erase(sql.find_last_not_of(" \t\n\r") + 1);
    if (sql.empty()) continue;
    sql += ';';
    YY_BUFFER_STATE bp = yy_scan_string(sql.c_str());
    if (bp == nullptr) {
      LOG(ERROR) << "Failed to create yy buffer state." << endl;
      return DB_FAILED;
    }
    yy_switch_to_buffer(bp);
    MinisqlParserInit();
    yyparse();
    if (MinisqlParserGetError()) {
      printf("%s\n", MinisqlParserGetErrorMessage());
      MinisqlParserFinish();
      yy_delete_buffer(bp);
      yylex_destroy();
      return DB_FAILED;
    }
    auto result = Execute(MinisqlGetParserRootNode());
    ExecuteInformation(result);
    MinisqlParserFinish();
    yy_delete_buffer(bp);
    yylex_destroy();
    sql_count++;
    if (result == DB_QUIT) {
      return DB_QUIT;
    }
  }
  auto total_end = std::chrono::system_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
  cout << "Total execution time: " << duration / 1000.0 << " sec (" << sql_count << " statements)" << endl;
  return DB_SUCCESS;
}
/**
 * TODO: Student Implement
 */
dberr_t ExecuteEngine::ExecuteQuit(pSyntaxNode ast, ExecuteContext *context) {
#ifdef ENABLE_EXECUTE_DEBUG
  LOG(INFO) << "ExecuteQuit" << std::endl;
#endif
  return DB_QUIT;
}
