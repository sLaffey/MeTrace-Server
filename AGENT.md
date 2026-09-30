# AGENT.md — 书影音迹 MeTrace（MeTrace-Server）服务端开发指南

> 本文件是项目的开发指南与需求基线，供两位开发者（以及辅助开发的 AI Agent）共同遵循。
> 接口路径、数据模型、数据结构要求以本文档为准；**新增接口或修改字段前，先更新本文档，再写代码**。

---

## 1. 项目概述

- **项目名**：MeTrace（书影音迹），仓库 `MeTrace-Server`（CMake 工程同名，可执行文件 `server`，产物在 `build/bin/server`）
- **全称**：个人书影音追踪与智能推荐系统
- **架构**：C/S 架构。服务端纯 C++ 实现，客户端 GUI 通过 HTTP + JSON 通信
- **服务端职责**：接收 HTTP 请求 → 在内存数据结构上执行业务与算法 → 返回 JSON；数据持久化采用**本地单个 JSON 文件**（启动全量加载，写操作后全量落盘）
- **课设定位**：《计算机科学与工程程序设计实践》数据结构与算法课设，**重心是数据结构与算法本身，而非工程化**：
  - 两人合作，人均代码量 ≥ 500 行，每人都要有能现场讲解的手写数据结构与算法
  - 至少 2 种手写数据结构（本项目计划 4~5 种，见 §8）
  - GUI 客户端、C/S 通信
  - 所有代码必须能现场解释

---

## 2. 技术栈与依赖

| 类别 | 技术选型 |
|---|---|
| 语言 | C++17 |
| HTTP 服务 | [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.57.1（header-only） |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) v3.12.0（header-only） |
| 构建 | CMake ≥ 3.22.1（`CMakeLists.txt` 中已写死此下限） |
| 编译器 | GCC ≥ 9 / Clang ≥ 10 / MSVC 2019+ |
| 平台 | Linux / macOS / Windows（优先 Linux） |
| 测试 | `tests/test_api.sh`（curl 断言）+ core 结构的简单 assert 程序 |

> 依赖由 CMake `FetchContent` 拉取源码并锁定版本，**不需要系统预装**，也不需要 `third_party/` 目录。
>
> ⚠️ **离线演示注意**：首次 `cmake` configure 需要联网，依赖缓存到 `build/_deps/`。
> 演示前先在目标机器跑通一次构建，之后**不要删除 `build/`**。

---

## 3. 目录结构

```text
MeTrace-Server/
├── CMakeLists.txt               # FetchContent 拉依赖 + 产物 build/bin/server
├── AGENT.md                     # 本文件
├── README.md
├── .gitignore                   # 忽略 build/、data/
├── include/                     # 只放头文件
│   ├── core/                    # 手写数据结构与数据模型
│   │   ├── Item.h               # ✓ 已有：条目模型（字段以此文件为准）
│   │   ├── LinkedList.h         # ✓ 已有骨架：单链表，主存储
│   │   ├── Tag.h                # ✓ 空文件：标签模型（见 §6.3）
│   │   ├── UndoStack.h          # 待实现：撤销/重做双栈
│   │   ├── Recommender.h        # 待实现：相似推荐算法
│   │   ├── Trie.h               # 可选：前缀搜索
│   │   ├── Heap.h               # 可选：二叉堆（排序 / TopN）
│   │   └── HashTable.h          # 可选：id → 条目 索引
│   ├── service/
│   │   └── DataBase.h           # ✓ 已有骨架：加载/保存 + CRUD + undo/redo 编排
│   └── http/
│       └── Router.h             # 待重建：路由注册（含参数校验与 JSON 编解码）
├── src/                         # 与 include/ 一一对应的实现 + main.cpp
│   ├── main.cpp                 # ✓ 已有：命令行参数解析 + 启动流程
│   ├── core/                    # Item.cpp（待完善）、UndoStack.cpp、Recommender.cpp …
│   ├── service/
│   │   └── DataBase.cpp         # ✓ 空壳待实现
│   └── http/
│       └── Router.cpp           # 待重建
├── scripts/
│   ├── build.sh                 # ✓ 已有
│   ├── run.sh                   # ✓ 已有（注释待更新为 json 路径）
│   └── seed_data.json           # 阶段 4：演示数据
├── tests/
│   ├── test_api.sh              # ✓ 已有（字段名需按 §6 适配）
│   ├── test_linkedlist.cpp      # 阶段 1：简单 assert 程序
│   ├── test_undostack.cpp       # 阶段 3
│   └── test_recommender.cpp     # 阶段 3
└── data/
    └── metrace.json             # 运行时生成（已 gitignore）
```

**现状**：`src/main.cpp` 的参数解析已完成，但引用的 `metrace::http::registerRoutes` 与 `include/http/Router.h` 尚未重建；
`CMakeLists.txt` 中仍残留 SQLiteCpp 的 FetchContent 与链接（阶段 0 清理，见 §12）。

铁律：头文件一律放 `include/`，实现一律放 `src/`，文件名与类名一致（大驼峰）。

---

## 4. 架构设计

三层结构，自上而下单向依赖：

```text
客户端 (GUI)
    │  HTTP + JSON
    ▼
┌─────────────────────────────────────┐
│   HTTP 层 (src/http/Router.cpp)     │
│   路由注册 · 参数校验 · JSON 编解码   │
└─────────────────────────────────────┘
    ▼
┌─────────────────────────────────────┐
│   业务层 (service/DataBase)          │
│   加载/保存 · CRUD 编排              │
│   撤销/重做 · 推荐入口               │
└─────────────────────────────────────┘
    ▼
┌─────────────────────────────────────┐
│   数据结构与算法层 (core/)           │
│   LinkedList · UndoStack            │
│   Recommender（可选 Trie/Heap/…）    │
│   Item / Tag 数据模型                │
└─────────────────────────────────────┘
    ▼
┌─────────────────────────────────────┐
│   本地 JSON 文件 (data/metrace.json) │
└─────────────────────────────────────┘
```

分层职责，**禁止跨层**：

| 层 | 位置 | 允许做的事 | 禁止做的事 |
|---|---|---|---|
| HTTP | `include/http/`、`src/http/` | 解析请求、校验参数、调用 DataBase、写响应 | 直接操作链表、读写文件 |
| Service | `include/service/`、`src/service/` | 持有数据结构、load/save、业务规则、undo/redo 编排、调用算法 | include httplib |
| Core | `include/core/`、`src/core/` | 数据结构与算法、Item/Tag 模型与序列化 | include httplib、关心 HTTP 状态码 |

Item 的 JSON 序列化（`toJson`/`fromJson`）放在 core 层，可依赖 nlohmann/json，但不依赖 httplib。

---

## 5. 数据存储设计

### 5.1 存储文件格式

单个 JSON 文件（默认 `./data/metrace.json`），启动时全量读入，写操作成功后全量覆写：

```json
{
  "next_id": 42,
  "items": [ { "id": 1, "type": "book", "...": "..." } ],
  "tags":  [ { "id": 1, "name": "科幻" } ]
}
```

- `next_id`：自增主键计数器，**持久化在文件里**，保证删除条目后 ID 不复用
- `items`：全部条目，字段见 §6.1
- `tags`：全局标签表，`name` 全局唯一

### 5.2 加载与保存

- **加载**：`DataBase(path)` 构造时读取文件 → 逐条 `Item::fromJson` → 插入链表；文件不存在按空库启动（不报错）
- **保存**：每个**写操作**（增/改/删/撤销/重做/新增标签）成功后调用 `save()` 全量覆写
- **必须原子写**：先写同目录临时文件（如 `metrace.json.tmp`），再 `std::filesystem::rename` 覆盖。
  这是防止写一半崩溃导致数据文件损坏的唯一保障，不允许直接截断原文件重写
- 数据规模按几十~几百条设计，全量覆写的性能完全够用，**不要为此做增量写或缓存**

### 5.3 线程安全 TODO

httplib 默认用线程池并发处理请求，而链表不是线程安全的。**整个 DataBase 持一把 `std::mutex`，
所有公开方法（含 undo/redo 与 save）进入即加锁**即可，锁粒度不用再细分——数据量级下不构成瓶颈。

### 5.4 生命周期

`main()` 中先构造 `DataBase` 并保持存活，把引用传给 `registerRoutes(server, db)`，
路由闭包捕获引用使用；禁止在函数栈上构造后任其析构。

---

## 6. 数据模型

### 6.1 Item（以 `include/core/Item.h` 为准）

| 字段 | C++ 类型 | JSON 类型 | 约定 |
|---|---|---|---|
| `id` | `int` | number | 服务端生成，只读 |
| `type` | `ItemType` 枚举 | string | `book` / `movie` / `music`；枚举中的 `Null` 仅作内部默认值，JSON 中不出现 |
| `title` | `string` | string | 必填，≤ 200 字符 |
| `author` | `string` | string | 作者/导演/艺术家，空串表示未知 |
| `description` | `string` | string | 简介，空串表示无 |
| `date` | `string` | string | 发布日期，推荐 `YYYY-MM-DD`，空串表示未知 |
| `progress` | `double` | number | 进度 0~1，默认 0（客户端负责显示为百分比） |
| `score` | `int` | number | **0 表示未评分**，有效评分 1~10 |
| `comment` | `string` | string | 短评，空串表示无 |
| `tags` | `vector<string>` | string[] | 标签**名称**数组，去重、按名称 UTF-8 字节序排序，单条目最多 20 个 |
| `created_at` | `string` | string | `YYYY-MM-DD HH:MM:SS`（UTC），只读 |
| `updated_at` | `string` | string | 同上，每次 PUT 自动刷新 |

> 与旧版（SQLite 时期）接口相比：`creator→author`、`year→date`、`review→comment`，
> 移除 `cover_url` 与 `status`，`score` 从可空 double 改为 int（0 = 未评分，不再使用 null 语义）。
> 所有"未知/为空"统一用空串或 0 表达，**JSON 中不出现 null**。

`Item.h` 待补齐（阶段 0）：`progress`/`tags` 的 setter；`id`/`date`/`author`/`description`/`created_at`/`updated_at` 的 getter；
`fromJson`/`toJson` 覆盖全部 12 个字段（当前 `Item.cpp` 只处理 4 个，且引用了不存在的成员 `name`，需修为 `title`）。

### 6.2 字段可编辑性

- **用户可通过 HTTP 修改的字段只有 4 个**：`progress`、`score`、`comment`、`tags`（对应 `Item.h` 顶部注释）
- 其余字段（`type`/`title`/`author`/`description`/`date`/`id`/时间戳）为只读，
  来源仅限创建时的 JSON（本地存储或用户上传的条目信息）
- 因此 `PUT /api/items/{id}` 只接受上述 4 个字段，其余字段出现在请求体中一律 400

### 6.3 Tag

标签第一版**不单独建模**：条目上存名称数组，全局标签表是 `{id, name}` 的简单列表
（打标签时自动创建不存在的名称，删除条目不级联删标签；"孤儿标签"属可接受的简化）。
`include/core/Tag.h` 暂留空文件，如后续需要再启用。

---

## 7. HTTP API 规范

### 7.1 通用约定

- 基础 URL：`http://127.0.0.1:8000`；业务接口统一 `/api` 前缀，`/` 与 `/ping` 不加
- 请求/响应：`application/json; charset=utf-8`；ID 为 JSON number
- 分页：列表类接口统一 `limit`（默认 20，上限 100）与 `offset`（默认 0）
- `total` 是**满足过滤条件的总条数**（不受分页影响），`items` 只含当前页
- 错误响应：`{ "error": "错误描述" }`；HTTP 状态码本身即语义（400 参数错误 / 404 不存在 / 500 服务端异常），body 不再重复 code
- ⚠️ httplib 对任何 ≥400 的响应都会回调 `set_error_handler` 且不清空 `res.body`，
  统一 404 兜底必须先 `if (!res.body.empty()) return;`，否则会覆盖业务层写好的 400 信息

### 7.2 接口列表

| 方法 | 路径 | 说明 | 参数 / 请求体 | 响应 |
|---|---|---|---|---|
| GET | `/` | 纯文本存活探测 | — | `MeTrace-Server is running` |
| GET | `/ping` | 健康检查 | — | `{"status":"ok","service":"MeTrace-Server"}` |
| GET | `/api/items` | 条目列表 | `?type=&tag=&sort=&limit=&offset=` | `{"total":N,"items":[Item,...]}` |
| POST | `/api/items` | 新增条目 | Item 的创建字段（`type`、`title` 必填） | `201` + `Item` |
| GET | `/api/items/{id}` | 单个条目 | — | `Item` |
| PUT | `/api/items/{id}` | 部分更新 | 仅 `progress`/`score`/`comment`/`tags`，字段不出现则保持原值 | `Item` |
| DELETE | `/api/items/{id}` | 删除条目 | — | `204`（无 body） |
| GET | `/api/tags` | 标签列表 | `?limit=&offset=` | `{"total":N,"items":[{id,name},...]}` |
| POST | `/api/tags` | 新增标签 | `{"name":"科幻"}`，同名幂等 | `201` + `{id,name}` |
| POST | `/api/undo` | 撤销 | — | `{"success":true,"action":"..."}` |
| POST | `/api/redo` | 重做 | — | `{"success":true,"action":"..."}` |
| GET | `/api/recommend` | 相似推荐 | `?item_id=&topN=` | `{"items":[Item,...]}` |
| GET | `/api/search` | 前缀搜索（计划） | `?q=&type=&limit=&offset=` | `{"total":N,"items":[Item,...]}` |
| GET | `/api/stats` | 统计聚合（可选） | `?type=` | `{total, by_type, score_dist}` |

### 7.3 参数取值

| 参数 | 取值 | 说明 |
|---|---|---|
| `type` | `book` / `movie` / `music` | 缺省不过滤 |
| `tag` | 标签名称 | 按名称精确匹配 |
| `sort` | `created_at` / `score` / `date` / `title`，前缀 `-` 为倒序 | 默认 `-created_at` |
| `limit` | 1~100 | 默认 20，越界截断 |
| `offset` | ≥ 0 | 默认 0 |
| `q` | 任意字符串 | 走 Trie 前缀匹配；为空返回空列表而不是全表 |
| `item_id` | 条目 id | `/api/recommend` 缺省时返回全局高分榜（冷启动兜底） |
| `topN` | 正整数，默认 10，上限 50 | 推荐条数 |

取值非法（如 `type=game`、`sort=unknown`）一律返回 400，不静默忽略。

### 7.4 撤销 / 重做语义

- 双栈在**进程内存**中，不按客户端会话隔离；服务重启即清空
- 只有写操作入栈：`POST/PUT/DELETE /api/items`、`POST /api/tags`；GET 一律不入栈
- `Action` 记录"如何反向执行"：操作类型 + 条目 id + 操作前/后快照（Update 记 before/after，
  Create 记 after，Delete 记 before）+ 一句中文描述（如 `新增《三体》`，供界面展示）
- 撤销 Create = 删除该条目；撤销 Update = 恢复 before；撤销 Delete = 重新插入 before；重做反之
- 栈空时返回 `400` + `{"error":"nothing to undo"}`（redo 同理）
- undo 栈容量上限 100，超出丢弃最旧一条
- 撤销/重做本身**不入栈**（重做栈只在新的写操作发生时清空，这是双栈的标准语义）

### 7.5 推荐语义

- 相似度打分（默认权重，可调）：`3 × 共同标签数 + 2 × 同作者 + 1 × 同类型`；
  同分时评分高者优先，仍未分（score=0）的条目排最后
- 实现方式二选一（都能满足算法讲解要求）：
  1. 直接遍历链表逐条打分，O(n·k)（k 为平均标签数）
  2. 构建邻接表图（共同标签/同作者连边），按共同邻居数排序，O(V+E)
- `item_id` 缺省时返回全库按 `score` 降序的高分榜（用排序或手写堆取 TopN）
- 响应 `items` 不含条目自身

### 7.6 示例

**POST /api/items**

```json
// 请求
{
  "type": "book",
  "title": "三体",
  "author": "刘慈欣",
  "date": "2006-01-01",
  "description": "地球文明三部曲第一部",
  "progress": 0.5,
  "score": 9,
  "comment": "震撼",
  "tags": ["科幻", "中国文学"]
}

// 响应 201 Created
{
  "id": 1,
  "type": "book",
  "title": "三体",
  "author": "刘慈欣",
  "description": "地球文明三部曲第一部",
  "date": "2006-01-01",
  "progress": 0.5,
  "score": 9,
  "comment": "震撼",
  "tags": ["中国文学", "科幻"],
  "created_at": "2026-09-29 10:00:00",
  "updated_at": "2026-09-29 10:00:00"
}
```

- `tags` 中不存在的标签自动创建并写入全局标签表；重复标签去重
- 未提供的可选字段用默认值（`progress`→`0`、`score`→`0`、字符串→空串、`tags`→`[]`）
- 响应必须回传完整条目（含服务端生成的 `id` 与时间戳），便于客户端刷新本地缓存

**PUT /api/items/{id}** —— 部分更新，只认 4 个可编辑字段：

```json
{ "progress": 1.0, "score": 10, "tags": ["科幻"] }
```

字段不出现则保持原值；`tags` 传 `[]` 即清空标签。**不使用 null 语义**，出现 null 或只读字段一律 400。

---

## 8. 核心数据结构与算法（课设重心）

### 8.1 必做

| 文件 | 结构/算法 | 关键操作与复杂度 | 用途 |
|---|---|---|---|
| `LinkedList.h/cpp` | 单链表 | 头插 O(1)；按 id 查找/删除 O(n) | 全部条目的主存储，所有 CRUD 都在链表上 |
| `UndoStack.h/cpp` | 双栈 | push/undo/redo 均摊 O(1)，容量 100 | 撤销/重做（语义见 §7.4） |
| `Recommender.h/cpp` | 相似度推荐 | 遍历打分 O(n·k) 或图共同邻居 O(V+E) | `/api/recommend`；高分榜用排序 O(n log n) |

### 8.2 可选（按人力与兴趣取舍，每人至少再认领一个）

| 文件 | 结构 | 关键操作与复杂度 | 用途 |
|---|---|---|---|
| `Trie.h/cpp` | 前缀树 | insert/search O(L) | `/api/search` 标题/作者前缀搜索 |
| `Heap.h/cpp` | 二叉堆 | push/pop O(log n)，top O(1) | 推荐与高分榜的 TopN；`sort=-score` |
| `HashTable.h/cpp` | 链地址法哈希表 | 平均 O(1)，负载因子 >0.75 扩容 | id → 条目的索引，加速单查/改/删 |

### 8.3 接口示例（可按实现微调，复杂度注释必须写）

```cpp
// include/core/LinkedList.h —— 单链表：主存储
template <typename T>
class LinkedList {
public:
    void insert(const T& value);          // 头插 O(1)
    bool removeById(int id);              // 按 id 摘除节点 O(n)
    T* findById(int id);                  // 未命中返回 nullptr O(n)
    std::vector<T*> toVector() const;     // 供列表过滤/排序/推荐遍历 O(n)
private:
    struct Node { T data; Node* next = nullptr; };
    Node* head_ = nullptr;
};
// 析构逐节点 delete；拷贝构造/赋值直接禁用（= delete），避免浅拷贝双重释放

// include/core/UndoStack.h —— 撤销/重做双栈
struct Action {
    enum class Kind { CreateItem, UpdateItem, DeleteItem, CreateTag };
    Kind        kind;
    int         itemId = 0;          // CreateTag 时存 tag id
    Item        before;              // 操作前快照（Update/Delete 用）
    Item        after;               // 操作后快照（Create/Update 用）
    std::string tagName;             // 仅 CreateTag 使用
    std::string description;         // 界面展示，如 "新增《三体》"
};

class UndoStack {
public:
    explicit UndoStack(std::size_t capacity = 100);
    void push(const Action& action);       // 入 undo 栈并清空 redo 栈；超容量丢最旧
    std::optional<Action> undo();          // 弹出待反向执行的操作
    std::optional<Action> redo();
    bool canUndo() const;
    bool canRedo() const;
};

// include/core/Recommender.h —— 相似推荐（权重见 §7.5）
class Recommender {
public:
    static std::vector<int> similar(const LinkedList<Item>& items, int itemId, int topN);
    static std::vector<int> topScored(const LinkedList<Item>& items, int topN);  // 冷启动高分榜
};
```

### 8.4 结构 → 真实调用方（验收会现场追问"这个类在哪里被调用"）

`LinkedList` → DataBase 的全部 CRUD；`UndoStack` → `/api/undo`、`/api/redo`；
`Recommender` → `/api/recommend`；`Trie` → `/api/search`；`Heap` → 推荐与高分榜 TopN；
`HashTable` → DataBase 单查/改/删的索引。**只写不用的结构不算数**。

---

## 9. 开发规范

### 9.1 代码风格

- 类名/文件名大驼峰 `ClassName`，函数小驼峰 `functionName`，变量小写下划线 `variable_name`
- 命名空间 `metrace::core` / `metrace::service` / `metrace::http`；禁止 `using namespace std;`
- 头文件 `#pragma once`，只 include 真正需要的东西
- 注释用中文；核心数据结构必须注明时间/空间复杂度
- 保持码量克制：**能 10 行解决的绝不写 30 行**，课设评分看结构与算法，不看分层花样

### 9.2 错误处理

- 业务错误（参数非法/不存在）由 Router 统一映射为 400/404 的 JSON 错误
- 文件读写失败打印 `std::cerr` 并让操作失败（500），不要静默吞掉

### 9.3 日志

访问日志记录方法、路径、状态码与耗时，输出到 `std::cerr` 即可。

### 9.4 Git 提交

提交信息格式 `[模块] 简要描述`，如 `[core] 实现 LinkedList 插入与删除`。定期提交，保留开发过程记录。

---

## 10. 构建与运行

`CMakeLists.txt` 要点（阶段 0 清理后的目标状态）：

- FetchContent 只拉 `httplib`（v0.57.1）与 `nlohmann_json`（v3.12.0），**删除 SQLiteCpp 的 Declare 与链接**
- `file(GLOB_RECURSE ... src/*.cpp)` 自动收集源文件，新增文件无需改 CMake
- 产物固定 `build/bin/server`；`-Wall -Wextra -Wpedantic`；Release 下 LTO

```bash
./scripts/build.sh          # 构建
./scripts/run.sh            # 启动（默认 127.0.0.1:8000 + ./data/metrace.json）
./scripts/run.sh --port 9000 --db /tmp/demo.json
./build/bin/server --help
```

启动参数（已在 `src/main.cpp` 实现，优先级 命令行 > 环境变量 > 默认值）：

| 参数 | 环境变量 | 默认值 | 说明 |
|---|---|---|---|
| `--host <addr>` | `METRACE_HOST` | `127.0.0.1` | 监听地址 |
| `--port <port>` | `METRACE_PORT` | `8000` | 监听端口（1~65535） |
| `--db <path>` | `METRACE_DB` | `./data/metrace.json` | JSON 存储文件路径（父目录自动创建） |

---

## 11. 测试策略

- **单元测试**：每个 core 结构一个简单 assert 程序（`tests/test_*.cpp`，普通 main + 断言即可，不引入 gtest），
  重点覆盖链表增删边界（空表/头节点/不存在 id）与双栈语义（undo 后 redo、新写操作清空 redo 栈、容量淘汰）
- **接口测试**：`tests/test_api.sh`（curl 断言），阶段 2 需按 §6 字段名适配（`creator→author`、`review→comment`、`year→date`、`score` 为 int）
- **集成演示**：加载 `scripts/seed_data.json`，验证增删改查、撤销重做、推荐
- **性能测试**（阶段 4）：1000 条数据下推荐与（若实现）搜索的响应时间

---

## 12. 开发任务清单（按阶段）

### 阶段 0：清理残留，恢复可编译（9.29~9.30）

- [ ] `CMakeLists.txt` 移除 SQLiteCpp 的 FetchContent 与链接
- [ ] `main.cpp` 默认路径改 `./data/metrace.json`，include `service/DataBase.h`；`DataBase` 由 main 持有并传引用给路由注册
- [ ] 修复 `Item.cpp`（`name`→`title`），补全 `toJson`/`fromJson` 全部 12 字段；`Item.h` 补 getter/setter（见 §6.1）
- [ ] 重建最小 `Router`（先只挂 `/` 与 `/ping`）
- [ ] `.gitignore` 改为忽略 `data/`；`run.sh` 注释更新
- 验收：`./scripts/build.sh` 通过，`/ping` 返回 200

### 阶段 1：数据层（10.1~10.3）

- [ ] `LinkedList`：insert / removeById / findById / toVector + `tests/test_linkedlist.cpp`
- [ ] `DataBase`：构造加载（文件不存在按空库）、`save()` 原子落盘（临时文件 + rename）
- [ ] `next_id` 持久化；标签表加载与 upsert
- 验收：手工构造 JSON 文件，启动加载正确、写操作后文件内容正确

### 阶段 2：HTTP CRUD 重建（10.4~10.7）

- [ ] `/api/items` 全部 5 个路由 + 参数校验 + 过滤/排序/分页（排序先直接实现，归并/快排可作为算法讲解点）
- [ ] `/api/tags` 两个路由（同名幂等）
- [ ] `tests/test_api.sh` 字段适配，37 项断言全过
- 验收：接口测试全部通过

### 阶段 3：撤销重做与推荐（10.8~10.12，两人并行）

- [ ] A 线：`UndoStack` + `/api/undo`、`/api/redo` + 写操作入栈改造 + `tests/test_undostack.cpp`
- [ ] B 线：`Recommender`（相似 + 高分榜）+ `/api/recommend` + `tests/test_recommender.cpp`
- [ ] 可选：`Trie` + `/api/search`，或 `Heap`/`HashTable` 接入现有链路
- 验收：新接口有测试断言；能指着代码讲清结构与复杂度

### 阶段 4：完善与验收（10.13~10.16）

- [ ] 50~100 条演示数据 `scripts/seed_data.json`
- [ ] 1000 条压测（推荐、搜索响应时间）
- [ ] README 与本文档核对一致；演示视频与讲稿
- [ ] 检查人均代码量 ≥ 500 行，准备现场代码讲解要点

---

## 13. 两人分工建议

| 成员 | 负责 | 对应结构与算法 |
|---|---|---|
| A | 存储与操作侧：链表 CRUD、JSON 序列化与持久化、撤销重做 | LinkedList、UndoStack（+ 可选 HashTable） |
| B | 算法侧：推荐、排序/TopN、（可选）前缀搜索 | Recommender（+ 可选 Heap / Trie） |

两人都要能独立讲清自己那部分的结构定义、操作实现与复杂度；HTTP 层（Router）由两人共同维护，谁先到谁写。

---

## 14. 验收标准与演示准备

- 服务端能独立启动并监听端口，所有接口返回 JSON，数据重启不丢
- 核心数据结构手写且**有真实调用方**，能现场解释代码与复杂度
- 撤销/重做、推荐功能可演示；`tests/test_api.sh` 全部通过
- 演示准备：提前 `./scripts/run.sh`；`seed_data.json` 灌数据；准备 curl 命令逐个展示 CRUD → 撤销重做 → 推荐；
  讲解顺序建议：链表节点结构 → 双栈撤销 → 推荐打分/图遍历

---

## 15. 注意事项

1. **不要使用 Python 中间层**，服务端纯 C++ 实现
2. **主存储是内存链表，JSON 文件是唯一持久化副本**；除保存/加载外，任何代码不得直接读写该文件
3. **核心数据结构必须手写**：`std::vector`/`std::stack` 可作底层容器，但链表的节点串接、双栈的语义、
   堆的上浮/下沉、哈希的散列与扩容、Trie 的节点、推荐打分逻辑必须自己写
4. 所有代码必须能现场解释，避免复制无法理解的代码
5. 写操作落盘必须走原子写（§5.2），这是硬性要求
6. 接口路径与响应格式以本文档 §7 为准；字段以 §6 为准；改文档先于改代码
7. 保持码量克制，宁少勿滥——删得动的抽象都删掉

---

## 16. 后续可能的开发方向

- 条目只读信息（标题/作者/日期/简介）从外部数据源（如 NeoDB）导入，丰富用户上传之外的创建途径
- 数据量或并发访问显著增长时，可将存储层替换为嵌入式数据库，上层 HTTP 与业务接口保持不变
