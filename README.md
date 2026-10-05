# MiniSQL

基于数据库系统课程框架完成的教学型关系数据库项目，主要使用 **cpp17**。

项目围绕数据库内核的主要环节展开：从磁盘页与缓冲池，到记录存储、B+树索引、元数据管理和 SQL 执行，理解一条 SQL 如何转化为实际的数据读写。

## 支持功能

- **数据库与表管理：** 创建、删除、查看和切换数据库；创建、删除和查看表。
- **数据读写：** 支持 `insert`、`select`、`update`、`delete`，以及条件过滤和指定列查询。
- **索引管理：** 创建、删除和查看 B+树索引；支持顺序扫描与索引扫描。
- **数据类型：** 支持 `int`、`float` 和 `char(n)`。
- **数据持久化：** 保存表数据与元数据，正常退出后可重新打开数据库。
- **命令行交互：** 提供 SQL 输入、查询结果展示和 SQL 文件执行入口。

## 主要实现

| 模块 | 实现内容 |
| --- | --- |
| 磁盘与页管理 | 位图页、磁盘页分配与回收、逻辑页与物理页映射 |
| 缓冲池 | 页面读取、固定与解除固定、脏页刷新，以及 LRU、CLOCK 置换策略 |
| 记录管理 | 字段、行和表结构的序列化与反序列化，表页与表堆的增删改查及迭代 |
| B+树索引 | 内部页与叶子页操作、插入与删除、分裂与合并、索引查找及迭代 |
| 元数据管理 | 表和索引的创建、查询、删除，以及元数据保存与加载 |
| SQL 执行 | 数据库与表管理命令，顺序扫描、索引扫描、插入、更新和删除执行器 |
| 并发控制 | 共享锁、排他锁、锁升级、两阶段锁规则、等待图与死锁检测 |
| 恢复算法 | 在简化键值模型中实现日志链、检查点、REDO 和 UNDO |

SQL 解析、部分基础接口与第三方依赖来自课程框架。并发控制和恢复算法目前主要通过模块测试验证，具体功能边界见下文。

## 构建环境

- **操作系统：** Linux、macOS；Windows 用户可通过 WSL 使用 Linux 环境。
- **编译器：** 支持 C++17 的 GCC 或 Clang。
- **构建工具：** CMake 3.20 或更高版本，建议使用 CMake 3.x。
- **第三方依赖：** 仓库已包含 GoogleTest 和 glog 源码。

## 编译与运行

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

构建完成后，启动命令行程序：

```bash
cd build
./bin/main
```

输入 SQL 时需要以分号结束，输入 `quit;` 退出。

当前解析器要求 **SQL 关键字小写、字符串使用双引号**。数据库文件保存在程序工作目录下的 `databases/` 中。

如需 Release 构建，在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 使用示例

以下示例已在当前版本验证，可在首次创建演示数据库时按顺序执行：

```sql
create database demo;
use demo;

create table students (
  id int unique,
  name char(32),
  score float
);

create index idx_students_id on students(id);

insert into students values (1, "Alice", 91.5);
insert into students values (2, "Bob", 85.0);

select * from students;
select name, score from students where id = 1;

update students set score = 90.0 where id = 2;
select * from students where score >= 90;

delete from students where id = 1;
select * from students;

show indexes;
quit;
```

执行完成后，表中保留 `Bob`，成绩为 `90.0`。再次启动程序，可读取已保存的数据：

```sql
use demo;
select * from students;
quit;
```

## 测试与验证

### 运行全部项目测试

完成构建后，在项目根目录执行：

```bash
cd build
./test/minisql_test
```

### 运行单个测试模块

例如，在项目根目录构建并运行 LRU 测试：

```bash
cmake --build build --target lru_replacer_test
cd build
./test/lru_replacer_test
```

也可通过 GoogleTest 过滤指定测试套件：

```bash
./test/minisql_test --gtest_filter=LockManagerTest.*
```

上述过滤命令从 `build/` 目录执行。

## 当前功能边界

- 当前 SQL 支持教学所需的基础语句，不包含 JOIN、子查询、聚合和完整 SQL 标准。
- SQL 层的 `begin`、`commit`、`rollback` 执行入口尚未实现，目前会返回失败。
- 锁管理模块已实现并通过测试，但尚未完整接入 SQL 执行流程。
- 恢复模块在简化键值模型中验证 REDO/UNDO，尚未形成与页存储、SQL 写入联动的完整持久化日志恢复流程。
- 现有测试验证特定模块与示例行为，尚未进行大规模性能基准测试。

## 源码结构

| 路径 | 内容 |
| --- | --- |
| `src/buffer/` | 缓冲池与页面置换 |
| `src/storage/`、`src/page/` | 磁盘、表堆及各类页面 |
| `src/record/` | 字段、行与表结构 |
| `src/index/` | B+树与索引迭代 |
| `src/catalog/` | 表和索引元数据 |
| `src/parser/`、`src/planner/` | SQL 解析与执行计划 |
| `src/executor/` | 命令处理与执行器 |
| `src/concurrency/` | 锁管理与事务状态管理 |
| `src/include/recovery/` | 日志记录与恢复算法 |
| `test/` | GoogleTest 测试 |
| `thirdparty/` | 第三方依赖 |

## License

[MIT License](LICENSE)，保留原有版权声明。第三方依赖遵循各自的许可证。
