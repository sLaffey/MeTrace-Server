# AGENT.md — 书影音迹 MeTrace（MeTrace-Server）服务端开发指南

> 本文件用于指导本地 AI Agent 辅助开发 **书影音迹 MeTrace** 的服务端项目。  
> 请严格遵循本文件中的架构、规范、接口与数据结构要求进行开发。

---

## 1. 项目概述

- **项目名**：MeTrace（书影音迹）
- **仓库名**：`MeTrace-Server`（CMake 工程同名；可执行文件为 `server`，产物在 `build/bin/server`）
- **全称**：个人书影音追踪与智能推荐系统
- **架构**：C/S 架构，服务端 C++ 实现，客户端 GUI 通过 HTTP + JSON 通信
- **服务端职责**：
  - 接收客户端 HTTP 请求
  - 处理业务逻辑
  - 操作 SQLite 数据库
  - 实现核心数据结构与算法（Trie、图、堆、栈、哈希表等）
  - 返回 JSON 响应
- **目标**：满足《计算机科学与工程程序设计实践》课设要求，展示数据结构与算法应用，代码量人均 ≥500 行，至少 2 种数据结构，GUI 客户端，C/S 通信。

---

## 2. 技术栈与依赖

| 类别 | 技术选型 |
|---|---|
| 语言 | C++17 |
| HTTP 服务 | [cpp-httplib](https://github.com/yhirose/cpp-httplib)（header-only） |
| JSON | [nlohmann/json](https://github.com/nlohmann/json)（header-only） |
| 数据库 | SQLite3（C API 或 SQLiteCpp） |
| 构建 | CMake ≥ 3.22.1（`CMakeLists.txt` 中已写死此下限） |
| 编译器 | GCC ≥ 9 / Clang ≥ 10 / MSVC 2019+ |
| 平台 | Linux / macOS / Windows（优先 Linux） |
| 测试 | 可选 GoogleTest 或简单脚本测试 |

> 依赖的实际引入方式：CMake `FetchContent` 直接拉取源码并锁定版本，**不使用** `third_party/` 目录，也不依赖系统预装的 SQLite3。
>
> | 依赖 | 锁定版本 | CMake 目标名 |
> |---|---|---|
> | cpp-httplib | v0.57.1 | `httplib::httplib` |
> | nlohmann/json | v3.12.0 | `nlohmann_json::nlohmann_json` |
> | SQLiteCpp | 3.4.0（内含 sqlite3 源码，无需 `find_package(SQLite3)`） | `SQLiteCpp` |
>
> ⚠️ **离线演示注意**：`FetchContent` 首次 `cmake` configure 需要联网，依赖会被缓存到 `build/_deps/`。
> 演示前务必先在目标机器上跑通一次 `cmake -S . -B build`，之后**不要删除 `build/`**，直接复用已有的 `build/_deps/` 即可离线构建。

---

## 3. 目录结构

```text
MeTrace-Server/
├── CMakeLists.txt           # 已存在：FetchContent 拉依赖 + 产物 build/bin/server
├── AGENT.md                 # 本文件
├── README.md                # 待补（阶段 4）
├── .gitignore               # 已存在（忽略 build/、*.db / *.db-wal / *.db-shm）
├── include/                 # 只放头文件
│   ├── core/                # 阶段 2：手写数据结构，全部待实现
│   │   ├── Trie.h
│   │   ├── Graph.h
│   │   ├── PriorityQueue.h
│   │   ├── HashTable.h
│   │   └── UndoStack.h
│   ├── db/                  # 已完成
│   │   ├── Database.h            # 连接管理（单例 + 全局锁 + 建表迁移）
│   │   ├── Models.h              # Item / Tag / ItemInput / ItemQuery 等纯数据结构
│   │   ├── ItemRepository.h      # items + item_tags + tags(upsert) 的所有 SQL
│   │   ├── TagRepository.h
│   │   └── ActionLogRepository.h
│   ├── service/
│   │   ├── ServiceError.h        # ValidationError / NotFoundError → 400 / 404
│   │   ├── ItemService.h         # 已完成
│   │   ├── SearchService.h       # 待实现（阶段 3）
│   │   ├── RecommendService.h    # 待实现（阶段 3）
│   │   ├── StatsService.h        # 待实现（阶段 3）
│   │   └── UndoService.h         # 待实现（阶段 3）
│   └── http/
│       ├── Router.h
│       └── JsonUtil.h            # 请求体解析、响应构造、错误响应
├── src/                     # 只放实现（含程序入口）
│   ├── main.cpp             # 已完成：解析 --port/--db/--host、开库、启动服务
│   ├── core/                # 阶段 2，待实现
│   │   ├── Trie.cpp
│   │   ├── Graph.cpp
│   │   ├── PriorityQueue.cpp
│   │   ├── HashTable.cpp
│   │   └── UndoStack.cpp
│   ├── db/                  # 已完成
│   │   ├── Database.cpp
│   │   ├── ItemRepository.cpp
│   │   ├── TagRepository.cpp
│   │   └── ActionLogRepository.cpp
│   ├── service/
│   │   ├── ItemService.cpp       # 已完成
│   │   ├── SearchService.cpp     # 待实现（阶段 3）
│   │   ├── RecommendService.cpp  # 待实现（阶段 3）
│   │   ├── StatsService.cpp      # 待实现（阶段 3）
│   │   └── UndoService.cpp       # 待实现（阶段 3）
│   └── http/                # 已完成
│       ├── Router.cpp
│       └── JsonUtil.cpp
├── data/
│   └── metrace.db             # SQLite 数据库（按 --db 运行时生成，已 gitignore）
├── scripts/
│   ├── build.sh
│   ├── run.sh
│   └── seed_data.sql          # 演示数据
└── tests/
    ├── test_trie.cpp
    ├── test_graph.cpp
    └── test_api.sh
```

> **现状**：`include/` 与 `src/` 已按上面的结构分离，阶段 1 的基础设施（CMake、DB 层、HTTP 层、命令行参数、构建/运行脚本）**已完成并跑通**；
> 标注「待实现」的文件属于阶段 2 / 阶段 3，见 §11。
> 铁律：头文件一律放 `include/`，实现一律放 `src/`，文件名与类名一致（大驼峰）。

---

## 4. 架构设计

```text
客户端 (GUI)
    │
    │ HTTP/JSON
    ▼
┌─────────────────────────────────────┐
│           cpp-httplib 路由层         │
│  (Router: 解析请求、调用 Service)    │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│           业务逻辑层 (Service)       │
│  ItemService / SearchService /      │
│  RecommendService / StatsService /  │
│  UndoService                        │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│      数据结构与算法层 (Core)         │
│  Trie / Graph / PriorityQueue /     │
│  HashTable / UndoStack             │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│      数据访问层 (Repository)         │
│  ItemRepository / TagRepository /   │
│  ActionLogRepository                │
│  ← 全项目只有这一层允许出现 SQL        │
└─────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────┐
│        数据存储层 (SQLite)           │
│  Database 封装类（单例 + DbLock）    │
└─────────────────────────────────────┘
```

---

## 5. 数据库设计（SQLite）

### 5.1 表结构

```sql
-- 条目表（书、影、音）
CREATE TABLE IF NOT EXISTS items (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    type        TEXT NOT NULL CHECK(type IN ('book','movie','music')),
    title       TEXT NOT NULL,
    creator     TEXT,                -- 作者/导演/艺术家
    year        INTEGER,
    cover_url   TEXT,
    description TEXT,
    status      TEXT DEFAULT 'wish' CHECK(status IN ('wish','doing','done','dropped')),
    progress    REAL DEFAULT 0 CHECK(progress >= 0 AND progress <= 1),      -- 统一为 0~1 的比例
    score       REAL CHECK(score IS NULL OR (score >= 0 AND score <= 10)),  -- 0~10，未评分为 NULL
    review      TEXT,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,   -- UTC，SQLite 原生格式 'YYYY-MM-DD HH:MM:SS'
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 标签表
CREATE TABLE IF NOT EXISTS tags (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE NOT NULL
);

-- 条目-标签关联表
CREATE TABLE IF NOT EXISTS item_tags (
    item_id INTEGER NOT NULL,
    tag_id  INTEGER NOT NULL,
    PRIMARY KEY (item_id, tag_id),
    FOREIGN KEY (item_id) REFERENCES items(id) ON DELETE CASCADE,
    FOREIGN KEY (tag_id)  REFERENCES tags(id)  ON DELETE CASCADE
);

-- 操作日志表（审计日志，**不参与**撤销/重做）
CREATE TABLE IF NOT EXISTS action_log (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    action     TEXT NOT NULL,        -- 操作类型：create_item / update_item / delete_item / create_tag ...
    payload    TEXT,                 -- JSON 格式的操作详情
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

**字段语义约定**

- **`progress`**：统一存 0~1 的浮点数（`0.5` 表示一半），客户端负责显示成百分比。
- **`score`**：未评分存 `NULL`，**不要用 0 代替**（0 表示"真的评了 0 分"）。
- **`updated_at`**：SQLite 没有 `ON UPDATE`，`DEFAULT CURRENT_TIMESTAMP` 只在 INSERT 时生效。
  所有 `UPDATE` 语句**必须显式写** `updated_at = CURRENT_TIMESTAMP`，否则时间戳会永远停在创建时间。
- **`action_log`**：仅供审计/展示。撤销重做由内存里的 `UndoStack` 实现（见 §6.2 与 §7.1），
  所以这张表不需要"已撤销"标记，也**不能**靠它实现 redo。进程重启后日志还在，但撤销栈会清空。

### 5.2 索引与 PRAGMA

```sql
CREATE INDEX idx_items_type    ON items(type);
CREATE INDEX idx_items_status  ON items(status);
CREATE INDEX idx_items_score   ON items(score);
CREATE INDEX idx_items_created ON items(created_at);
CREATE INDEX idx_item_tags_tag ON item_tags(tag_id);
```

- 外键约束在 SQLite 里**默认关闭**，连接建立后必须执行 `PRAGMA foreign_keys = ON;`，否则 `ON DELETE CASCADE` 不会生效。
- 建议同时开启 WAL 并设置忙等待：`PRAGMA journal_mode = WAL;`、`PRAGMA synchronous = NORMAL;`，
  连接时传入 busy timeout（如 5000ms）。

### 5.3 建表与迁移的实现要点

**必须用代码建表，不要手工执行 `sqlite3 xxx.sql`**，否则代码里的 schema 和实际数据库很容易不一致。

- 所有建表语句写在 `Database::migrate()` 里，用 `PRAGMA user_version` 记录 schema 版本做增量迁移：
  `if (version < 1) { ...建表...; PRAGMA user_version = 1; }`，以后加表/加字段就继续追加 `if (version < N)` 分支。
- 每个迁移分支用 `SQLite::Transaction` 包裹，要么全部成功要么全部回滚。
- 一律使用 `CREATE TABLE IF NOT EXISTS` / `CREATE INDEX IF NOT EXISTS`，保证 `open()` 可以重复调用（幂等）。

### 5.4 数据存储层的类设计

分两层，**SQL 只允许出现在 Repository 里**：

1. **`Database`（`include/db/Database.h`）** —— 连接管理，进程内单例
   - `static Database& instance()`
   - `void open(const std::string& path, int busyTimeoutMs = 5000)`：打开/创建库，内部完成 PRAGMA 与迁移，幂等
   - `SQLite::Database& handle()`：取底层连接，未 `open()` 时抛异常
   - `void migrate(SQLite::Database& db)`：私有，实现见 §5.3
2. **`DbLock`（同一个头文件）** —— 作用域锁，基于 `std::recursive_mutex`
   - ⚠️ `SQLite::Database` / `SQLite::Statement` **都不是线程安全的**，而 httplib 默认用线程池并发处理请求，
     所以 Repository 的每个方法第一行必须是 `DbLock lock;`
   - 用递归锁是为了避免 `handle()` 内部加锁与外部 `DbLock` 叠加时自锁死
3. **`XxxRepository`** —— 每张表（或一组强关联的表）一个类
   - 例如 `ItemRepository` 同时负责 `items` 与 `item_tags`，`TagRepository` 负责 `tags`，`ActionLogRepository` 负责 `action_log`
   - 全部使用预编译语句 + 命名参数绑定（`:title`），**任何情况下不得拼接 SQL 字符串**
   - 出错抛 `SQLite::Exception`，由 HTTP 层的异常处理器统一转成 500
   - 查询返回 `std::optional<T>` 或 `std::vector<T>`，不把原始结果行暴露给上层
   - 一次操作涉及多张表（如 items + item_tags）时，必须放在同一个 `SQLite::Transaction` 里

> 单例 + 全局锁是"够用且好讲"的方案；若以后要提升并发，可以改成每线程一个连接（连接池），上层接口不用变。

---

## 6. HTTP API 规范

### 6.1 通用约定

- 基础 URL：`http://127.0.0.1:8000`
- 路径前缀：所有业务接口统一加 `/api` 前缀（如 `GET /api/items`）；非业务路由 `/`、`/ping` 不加前缀
- 请求/响应格式：`application/json; charset=utf-8`
- 字符编码：UTF-8
- ID 类型：JSON 中一律用数字（服务端内部为 `int64_t`）
- 时间格式：`YYYY-MM-DD HH:MM:SS`，**UTC**，直接使用 SQLite 原生格式
  （不做时区偏移，客户端自行按本地时区显示；不要改成 ISO8601 的 `T`/`Z`，避免来回转换出错）
- 分页：列表类接口统一支持 `limit`（默认 20，最大 100）与 `offset`（默认 0）
- 错误响应格式：
  ```json
  { "error": "错误描述" }
  ```
  HTTP 状态码本身即语义（400 参数错误 / 404 不存在 / 500 服务端异常），
  **不再**在 body 里重复 `code` 字段，避免两者不一致。
- ⚠️ 实现提醒：httplib 对**任何** `>=400` 的响应都会回调 `set_error_handler`，且调用前**不会**清空 `res.body`。
  统一 404 兜底必须先判断 `if (!res.body.empty()) return;`，否则会把业务代码自己写好的 400 校验信息覆盖掉。

### 6.2 接口列表

| 方法 | 路径 | 说明 | 请求体/参数 | 响应 |
|---|---|---|---|---|
| GET | `/ping` | 健康检查 | - | `{"status":"ok","service":"MeTrace-Server"}` |
| GET | `/api/items` | 获取条目列表 | `?type=&status=&tag=&sort=&limit=&offset=` | `{"total":N,"items":[{item},...]}` |
| GET | `/api/items/{id}` | 获取单个条目 | - | `{item}` |
| POST | `/api/items` | 新增条目 | `{type,title,creator,year,cover_url,description,status,progress,score,review,tags}` | 201 + `{item}` |
| PUT | `/api/items/{id}` | 修改条目 | 同上，字段均可选（部分更新） | `{item}` |
| DELETE | `/api/items/{id}` | 删除条目 | - | 204（无 body） |
| GET | `/api/search` | 前缀搜索 | `?q=&type=&limit=&offset=` | `{"total":N,"items":[{item},...]}` |
| GET | `/api/recommend` | 获取推荐 | `?item_id=&topN=` | `{"items":[{item},...]}` |
| GET | `/api/stats` | 统计信息 | `?year=` | `{total, by_type, by_status, score_dist, ...}` |
| POST | `/api/undo` | 撤销 | - | `{"success":true,"action":"..."}` |
| POST | `/api/redo` | 重做 | - | `{"success":true,"action":"..."}` |
| GET | `/api/tags` | 获取所有标签 | `?limit=&offset=` | `{"total":N,"items":[{id,name},...]}` |
| POST | `/api/tags` | 新增标签 | `{name}` | 201 + `{id,name}` |

**参数取值约定**

| 参数 | 取值 | 说明 |
|---|---|---|
| `type` | `book` / `movie` / `music` | 缺省表示不过滤 |
| `status` | `wish` / `doing` / `done` / `dropped` | 缺省表示不过滤 |
| `tag` | 标签**名称** | 按名称精确匹配，缺省表示不过滤 |
| `sort` | `score` / `year` / `created_at` / `title`，带 `-` 前缀表示倒序，如 `-score` | 默认 `-created_at` |
| `q` | 任意字符串 | 走 Trie 前缀匹配；为空时返回空列表而不是全表 |
| `item_id` | 条目 id | `/api/recommend` 缺省时返回全局高分榜（冷启动兜底） |
| `topN` | 正整数，默认 10，上限 50 | 推荐条数 |
| `year` | 四位年份 | `/api/stats` 传此参数时只统计该年 |

**撤销/重做的语义（重要）**

- 撤销栈是**进程级内存栈**，不按客户端会话隔离。多个客户端同时操作会互相影响，
  演示时用一个客户端，或提前说明这是已知简化（课设范围内可接受）。
- 只有会产生数据变更的操作才入栈：`POST/PUT/DELETE /api/items`、`POST /api/tags`；GET 一律不入栈。
- 栈空时 `/api/undo` 返回 `400` + `{"error":"nothing to undo"}`，`/api/redo` 同理。
- 服务重启后撤销栈清空（`action_log` 表仍保留历史记录）。
- 撤销栈必须设容量上限（如 100 条），超出时丢弃最旧的一条，防止内存无限增长。

### 6.3 示例请求/响应

**POST /api/items**
```json
// 请求
{
  "type": "book",
  "title": "三体",
  "creator": "刘慈欣",
  "year": 2006,
  "status": "doing",
  "progress": 0.5,
  "score": 9.5,
  "review": "震撼",
  "tags": ["科幻", "中国文学"]
}

// 响应（201 Created）
{
  "id": 1,
  "type": "book",
  "title": "三体",
  "creator": "刘慈欣",
  "year": 2006,
  "cover_url": null,
  "description": null,
  "status": "doing",
  "progress": 0.5,
  "score": 9.5,
  "review": "震撼",
  "tags": ["科幻", "中国文学"],
  "created_at": "2026-09-22 10:00:00",
  "updated_at": "2026-09-22 10:00:00"
}
```

- `tags` 里**不存在的标签自动创建**，且与条目的插入放在同一个事务里；重复标签会去重。
- 响应必须回传完整 item（含服务端生成的 `id`、`created_at`、`updated_at`），不要只回 `{"id":1}`。
- 字段缺失时用默认值（`status`→`wish`、`progress`→`0`、`score`→`null`），非法枚举值返回 400。

**GET /api/search?q=三体**
```json
{
  "total": 2,
  "items": [
    { "id": 1, "type": "book", "title": "三体", "creator": "刘慈欣", "status": "done", "score": 9.5 },
    { "id": 2, "type": "book", "title": "三体II：黑暗森林", "creator": "刘慈欣", "status": "wish", "score": 9.0 }
  ]
}
```

**GET /api/items?type=book&sort=-score&limit=1&offset=0**
```json
{
  "total": 37,
  "items": [ { "id": 1, "title": "三体", "type": "book", "score": 9.5 } ]
}
```

> `total` 是**满足过滤条件的总条数**（不分页），用于客户端计算页数；`items` 只包含当前页。
> 实现时这是两条 SQL：一条 `SELECT COUNT(*)`，一条带 `LIMIT/OFFSET` 的查询。

---

## 7. 核心数据结构与算法

### 7.1 必须手写实现的数据结构

| 文件 | 数据结构 | 关键操作 | 用途 |
|---|---|---|---|
| `Trie.h/cpp` | 前缀树 | insert, search, startsWith | 标题/作者/标签前缀搜索 |
| `Graph.h/cpp` | 邻接表图 | addEdge, bfs, dfs, recommend | 书影音关联、相似推荐 |
| `PriorityQueue.h/cpp` | 二叉堆 | push, pop, top, heapifyUp/Down | 待看提醒、评分/年份排序、推荐 TopN |
| `HashTable.h/cpp` | 哈希表（链地址法） | insert, find, erase, rehash | 条目 ID → Item 的内存缓存、标签映射 |
| `UndoStack.h/cpp` | 双栈 | push, undo, redo, canUndo/canRedo | 撤销/重做（内存栈，语义见 §6.2） |

> ⚠️ 这些结构必须**真正承担业务逻辑**，不能只写不用——验收会现场追问"这个类在哪里被调用到"。对应关系：
> `Trie` → `SearchService` 的前缀搜索；`Graph` → `RecommendService` 的相似推荐；
> `PriorityQueue` → `/api/items?sort=` 与 `/api/recommend` 的 TopN 排序；
> `HashTable` → `ItemService` 的条目内存缓存；`UndoStack` → `/api/undo`、`/api/redo`。

### 7.2 算法要求

- **Trie 前缀搜索**：时间复杂度 O(L)，L 为字符串长度。
- **图 BFS/DFS**：时间复杂度 O(V+E)。
- **堆排序**：时间复杂度 O(n log n)。
- **哈希查找**：平均 O(1)。
- **相似度推荐**：基于标签重叠度、共同创作者、类型相似度。
- **统计聚合**：按类型、状态、评分区间、年份聚合。

### 7.3 类接口示例

```cpp
// include/db/Models.h —— 纯数据结构，不依赖 sqlite / json / httplib
struct Item {
    int64_t     id = 0;
    std::string type;             // book / movie / music
    std::string title;
    std::string creator;
    int         year = 0;         // 0 表示未知
    std::string coverUrl;
    std::string description;
    std::string status;           // wish / doing / done / dropped
    double      progress = 0.0;   // 0~1
    std::optional<double> score;  // 未评分为 nullopt
    std::string review;
    std::vector<std::string> tags;
    std::string createdAt;        // 'YYYY-MM-DD HH:MM:SS' (UTC)
    std::string updatedAt;
};

/// 撤销栈里存的操作：记录"如何反向执行"
struct Action {
    enum class Kind { CreateItem, UpdateItem, DeleteItem, CreateTag };
    Kind        kind;
    int64_t     entityId = 0;
    json        before;         // 操作前的快照（Update / Delete 用）
    json        after;          // 操作后的快照（Create / Update 用）
    std::string description;    // 给界面显示，如 "新增《三体》"
};

// include/core/Trie.h —— 前缀树：插入/查询 O(L)，L 为字符串长度
class Trie {
public:
    void insert(const std::string& text, int64_t itemId);
    std::vector<int64_t> search(const std::string& prefix) const;  // 所有以 prefix 开头的条目 id（去重）
    bool startsWith(const std::string& prefix) const;
private:
    struct Node {
        // 用 unique_ptr 管理子节点，析构时自动递归释放，避免裸指针内存泄漏
        std::unordered_map<char, std::unique_ptr<Node>> children;
        std::vector<int64_t> itemIds;  // 经过该节点的条目 id
    };
    std::unique_ptr<Node> root_ = std::make_unique<Node>();
    // 中文按 UTF-8 字节切分即可满足前缀匹配；英文需先统一大小写
};

// include/core/Graph.h —— 邻接表图：addEdge O(1)，BFS/DFS O(V+E)
class Graph {
public:
    void addEdge(int64_t a, int64_t b);
    std::vector<int64_t> bfs(int64_t start) const;
    std::vector<int64_t> recommend(int64_t itemId, int topN) const;  // 按共同邻居数降序
private:
    std::unordered_map<int64_t, std::vector<int64_t>> adj_;
};

// include/core/PriorityQueue.h —— 二叉堆：push/pop O(log n)，top O(1)
class PriorityQueue {
public:
    // less 返回 true 表示 a 的优先级低于 b（堆顶为"最大"者）
    explicit PriorityQueue(std::function<bool(const Item&, const Item&)> less);
    void push(const Item& item);
    Item pop();
    const Item& top() const;
    bool empty() const;
    std::size_t size() const;
private:
    std::vector<Item> heap_;
    std::function<bool(const Item&, const Item&)> less_;  // 支持按评分 / 年份 / 创建时间排序
    void heapifyUp(std::size_t i);
    void heapifyDown(std::size_t i);
};

// include/core/UndoStack.h —— 撤销/重做双栈：push/undo/redo 均为 O(1)
class UndoStack {
public:
    explicit UndoStack(std::size_t capacity = 100);
    void push(const Action& action);       // 入 undo 栈并清空 redo 栈；超容量时丢弃最旧的
    std::optional<Action> undo();          // 弹出待反向执行的操作
    std::optional<Action> redo();
    bool canUndo() const;
    bool canRedo() const;
private:
    std::stack<Action> undoStack_;
    std::stack<Action> redoStack_;
    std::size_t capacity_;
};

// include/core/HashTable.h —— 手写链地址法哈希表：平均查找 O(1)，最坏 O(n)
template <typename V>
class HashTable {
public:
    bool insert(int64_t key, const V& value);
    V* find(int64_t key);          // 未命中返回 nullptr
    bool erase(int64_t key);
    std::size_t size() const;
private:
    struct Entry { int64_t key; V value; };
    std::vector<std::list<Entry>> buckets_;
    std::size_t count_ = 0;
    std::size_t hash(int64_t key) const;   // 乘法散列
    void rehash();                         // 负载因子 > 0.75 时扩容为两倍
};
```

---

## 8. 开发规范

### 8.1 代码风格
- 遵循 Google C++ Style Guide。
- 类名大驼峰 `ClassName`，函数名小驼峰 `functionName`，变量名小写下划线 `variable_name`。
- **文件名与类名一致，也用大驼峰**：`Database.h`、`ItemRepository.cpp`、`UndoStack.h`
  （不要写 `database.h`、`note_repository.cpp` 这种小写下划线文件名）。
- 命名空间：`metrace::core` / `metrace::db` / `metrace::service` / `metrace::http`；禁止 `using namespace std;`。
- 头文件使用 `#pragma once`，并且只 include 自己真正需要的东西。
- **分层纪律**：`src/http/` 与 `src/service/` 里**不允许**出现任何 SQL 字符串，也不允许 include `SQLiteCpp`；
  数据库访问只能通过 `include/db/` 下的 Repository 类。
- 所有核心数据结构必须有详细注释，说明时间/空间复杂度。
- 注释用中文；对外接口用 Doxygen 风格 `///`。

### 8.2 错误处理
- 使用异常处理关键错误，或返回 `std::optional` / 错误码。
- HTTP 层统一捕获异常，返回 JSON 错误。
- 数据库操作必须检查返回值。

### 8.3 日志
- 简单日志输出到 `std::cerr` 或文件。
- 记录请求方法、路径、状态码、耗时。

### 8.4 Git 提交
- 提交信息格式：`[模块] 简要描述`，如 `[core] 实现 Trie 插入与搜索`。

---

## 9. 构建与运行

### 9.1 CMakeLists.txt 要点

```cmake
cmake_minimum_required(VERSION 3.22.1)

project(MeTrace-Server VERSION 0.0.1 DESCRIPTION "MeTrace backend server" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)                        # 生成 compile_commands.json 给 clangd 用
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)   # 产物固定为 build/bin/server

# 第三方依赖：全部用 FetchContent 拉源码并锁版本，不依赖系统预装（见 §2）
include(FetchContent)
FetchContent_Declare(httplib       GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git  GIT_TAG v0.57.1 GIT_SHALLOW TRUE)
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git        GIT_TAG v3.12.0 GIT_SHALLOW TRUE)
FetchContent_Declare(SQLiteCpp     GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git  GIT_TAG 3.4.0)
FetchContent_MakeAvailable(httplib nlohmann_json SQLiteCpp)

find_package(Threads REQUIRED)   # httplib 在 Linux 上依赖 pthread

# 自动收集 src/ 下的所有 .cpp：以后新增文件无需改 CMakeLists
file(GLOB_RECURSE METRACE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(server ${METRACE_SOURCES})

# 让 #include "db/Database.h" 这类项目内相对路径可用
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(server PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json
    SQLiteCpp
    Threads::Threads
)
```

> 编译告警（`-Wall -Wextra -Wpedantic`）、Release 下的 LTO、`install`/CPack 等已在仓库现有 `CMakeLists.txt` 中配好，不要重复添加。

### 9.2 构建脚本 `scripts/build.sh`

```bash
#!/bin/bash
# 配置并编译。首次执行需要联网，FetchContent 会把依赖拉到 build/_deps/ 并缓存。
set -e
cd "$(dirname "$0")/.."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo "构建完成: $(pwd)/build/bin/server"
```

### 9.3 运行脚本 `scripts/run.sh`

```bash
#!/bin/bash
# 默认 127.0.0.1:8000 + ./data/metrace.db（父目录会自动创建）
# 额外参数会透传，例如：./scripts/run.sh --port 9000 --db /tmp/demo.db
set -e
cd "$(dirname "$0")/.."

exec ./build/bin/server "$@"
```

### 9.4 启动参数

| 参数 | 默认值 | 说明 |
|---|---|---|
| `--port` | `8000` | 监听端口 |
| `--host` | `127.0.0.1` | 监听地址 |
| `--db` | `./data/metrace.db` | SQLite 数据库文件路径 |

- 优先级：**命令行参数 > 环境变量 > 默认值**（环境变量 `METRACE_PORT` / `METRACE_HOST` / `METRACE_DB` 作为兜底）。
- 已在 `src/main.cpp` 实现：`--help` 打印用法；参数缺值、端口不在 1~65535 时打印原因并退出（返回码 1）。
- 默认路径 `./data/metrace.db` 的父目录可能不存在，启动时会用
  `std::filesystem::create_directories(path.parent_path())` 自动创建，不会因为目录缺失启动失败。

---

## 10. 测试策略

- **单元测试**：对 Trie、Graph、PriorityQueue、HashTable、UndoStack 编写简单测试。
- **接口测试**：使用 `tests/test_api.sh`（已就绪，覆盖 CRUD / 分页过滤 / 参数校验）或直接 `curl` 测试所有 HTTP 接口。
- **集成测试**：启动服务端，使用演示数据，验证增删改查、搜索、推荐、统计。
- **性能测试**：插入 1000 条数据，测试搜索和推荐响应时间。
- **并发测试**：用 `xargs -P` 并发打接口，确认全局锁生效、无 `SQLITE_BUSY`：
  并发 POST 50 条应全部返回 201，`GET /api/items` 的 `total` 应为 50。

---

## 11. 开发任务清单（按阶段）

### 阶段 1：项目搭建（9.21~9.27）
- [x] 配置 CMakeLists.txt（FetchContent 引入 httplib / nlohmann_json / SQLiteCpp，产物 `build/bin/server`）
- [x] 实现最简单的 HTTP 路由 `/ping` 返回 `{"status":"ok"}`
- [x] 结构调整：建立 `include/` + `src/` 目录，入口拆成 `src/main.cpp`，路由拆成 `src/http/Router.cpp` + `src/http/JsonUtil.cpp`
- [x] 删除临时演示代码（`notes` 表 / `NoteRepository` / 5 个 `/api/notes` 路由）
- [x] 迁移 DB 层到 `include/db/Database.h` + `src/db/Database.cpp`，方法名 camelCase，`migrate()` 建 items / tags / item_tags / action_log
- [x] 实现命令行参数 `--port/--db/--host/--help`，并按 `--db` 自动创建父目录
- [x] 编写 `scripts/build.sh`、`scripts/run.sh`
- [x] 顺手打通 items / tags 的完整读写链路（`ItemRepository` / `TagRepository` / `ActionLogRepository` + `ItemService` + 全部 `/api/items`、`/api/tags` 路由）
- [x] 编写 `tests/test_api.sh`（37 项断言，覆盖 CRUD、分页过滤、参数校验、级联删除）

### 阶段 2：核心数据结构（9.28~10.5）
- [ ] 实现 Trie
- [ ] 实现 Graph
- [ ] 实现 PriorityQueue
- [ ] 实现 HashTable
- [ ] 实现 UndoStack
- [ ] 为每个数据结构编写单元测试
- [ ] 每个结构都要有**真实调用方**（对应关系见 §7.1 的提醒），不要只写不用

### 阶段 3：业务逻辑与 API（10.6~10.12）
- [x] 实现 `ItemRepository`（items + item_tags + tags 的事务写入）、`TagRepository`、`ActionLogRepository`
- [x] 实现 ItemService：增删改查 + 标签关联
- [ ] 实现 SearchService：基于 Trie 的搜索
- [ ] 实现 RecommendService：基于 Graph 的推荐
- [ ] 实现 StatsService：统计聚合
- [ ] 实现 UndoService：撤销/重做（内存 `UndoStack`，语义见 §6.2）
- [ ] 补齐 `/api/search`、`/api/recommend`、`/api/stats`、`/api/undo`、`/api/redo` 五个路由
- [ ] 给 `/api/search` 等新接口补 `tests/test_api.sh` 用例

### 阶段 4：完善与验收准备（10.13~10.16）
- [ ] 准备 50~100 条演示数据（SQL 或 JSON）
- [ ] 优化错误处理与日志
- [ ] 编写 README
- [ ] 录制演示视频
- [ ] 检查代码量，确保 ≥500 行
- [ ] 准备现场解释代码的要点

---

## 12. 验收标准与演示准备

### 12.1 验收标准
- 服务端能独立启动，监听端口。
- 所有 HTTP 接口可用，返回 JSON。
- 核心数据结构手写实现，能解释代码。
- 数据库操作正确，数据持久化。
- 搜索、推荐、统计功能正常。
- 代码量达标，结构清晰。

### 12.2 演示准备
- 提前启动服务端：`./scripts/run.sh`
- 准备演示数据：`sqlite3 data/metrace.db < scripts/seed_data.sql`（或用 `POST /api/items` 批量灌数据）
- 准备 `curl` 命令或 Postman 集合，展示接口调用。
- 准备代码讲解：打开 `Trie.cpp`、`Graph.cpp`，解释节点结构、插入、搜索、BFS。
- 说明 C/S 架构：客户端 GUI → HTTP → 服务端 → SQLite。

---

## 13. 注意事项

1. **不要使用 Python 中间层**，服务端纯 C++ 实现。
2. **数据库为主存储**，JSON 仅用于 HTTP 通信和配置文件。
3. **核心数据结构必须手写**，不要直接用 STL 替代所有逻辑（`std::stack`/`std::vector` 用于底层容器是可以的，
   但堆的上浮/下沉、哈希的散列与扩容、Trie 的节点、图的遍历必须自己写）。
4. **所有代码必须能现场解释**，避免复制无法理解的代码。
5. **提前准备离线数据**，NeoDB 查询作为可选功能，不依赖网络演示；`FetchContent` 的依赖也要提前下载并保留 `build/_deps/`（见 §2）。
6. **保持代码整洁**，注释清晰，便于报告撰写。
7. **定期提交 Git**，保留开发过程记录（提交信息格式见 §8.4）。
8. **不要在 `service/`、`http/` 里写 SQL**，也不要绕过 `DbLock` 直接访问数据库连接（见 §5.4、§8.1）。
9. **接口路径与响应格式以本文档 §6 为准**，新增接口前先更新 §6.2 的表格，避免文档与代码再次不一致。

---

> 本文件将随开发进展更新。Agent 在生成代码时，请严格遵循以上规范。  
> 如有疑问，优先参考课设要求与任务书。