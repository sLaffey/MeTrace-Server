# AGENT.md — 书影音迹 MeTrace（MeTrace-Server）服务端开发指南

> 本文件是项目的开发指南与需求基线，供两位开发者以及任何后续接手的 AI Agent 共同遵循。
> 接手开发的 Agent 请先读 §3「现状快照」定位进度，再按 §13 任务清单继续；**已定型的设计决策（§3.3）不要推翻重议**，有疑问先读对应章节的设计理由。
> 接口路径、数据模型、数据结构要求以本文档为准；**新增接口或修改字段前，先更新本文档，再写代码**。

---

## 1. 项目概述

- **项目名**：MeTrace（书影音迹），仓库 `MeTrace-Server`（CMake 工程同名，可执行文件 `server`，产物在 `build/bin/server`）
- **全称**：个人书影音追踪与智能推荐系统
- **架构**：C/S 架构。服务端纯 C++ 实现，客户端 GUI 通过 HTTP + JSON 通信
- **服务端职责**：接收 HTTP 请求 → 在内存数据结构上执行业务与算法 → 返回 JSON；数据持久化采用**本地单个 JSON 文件**（启动全量加载，写操作后全量原子落盘）
- **课设定位**：《计算机科学与工程程序设计实践》数据结构与算法课设，**重心是数据结构与算法本身，而非工程化**：
  - 两人合作，人均代码量 ≥ 500 行，每人都要有能现场讲解的手写数据结构与算法
  - 手写结构：链表、Trie、堆、哈希表、双栈、推荐算法（见 §9）
  - GUI 客户端、C/S 通信；所有代码必须能现场解释；能 10 行解决的绝不写 30 行

---

## 2. 技术栈与依赖

| 类别 | 技术选型 |
|---|---|
| 语言 | C++17 |
| HTTP 服务 | [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.57.1（header-only） |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) v3.12.0（header-only） |
| 构建 | CMake ≥ 3.22.1（开发机 Linux + GCC） |
| 测试 | `tests/test_api.sh`（curl 断言）+ core 结构的简单 assert 程序（不引框架） |

依赖由 CMake `FetchContent` 拉取源码并锁定版本，不需要系统预装。SQLiteCpp 已移除，**不要重新引入任何数据库依赖**（见 §17）。

> ⚠️ **离线演示注意**：首次 `cmake` configure 需要联网，依赖缓存到 `build/_deps/`。演示前先跑通一次构建，之后不要删除 `build/`。

---

## 3. 现状快照（2026-10-01）

### 3.1 已完成

- [x] CMake 清理：仅 FetchContent httplib + nlohmann_json；产物 `build/bin/server`
- [x] 全部头文件均有 `#pragma once`（铁律：头文件第一行，新文件当天加）
- [x] `src/main.cpp`：参数/环境变量解析、`db.load()` fail-fast → `db.save()`；HTTP 启动段随 Router 完成度打开
- [x] 数据模型定稿（`include/core/Item.h` 为权威，见 §7）：type 为字符串、score 为 int [1-100]、`Tag` 为 `std::string` 别名
- [x] `LinkedList`：insert（头插）/ remove（按 operator==，即按 id）
- [x] `Trie`：insert / query / remove（`removeRec` 递归释放空节点方案已定）
- [x] `Heap`：固定容量模板堆，`std::function` 比较器注入，淘汰式 `bool push`，siftUp/siftDown，拷贝已删
- [x] `DataBase::load`：全量加载 items / tags / next_id
- [x] `http/Router` 骨架起步（`/`、部分 `/api/items`）

### 3.2 待办（按优先级，展开见 §13）

1. Item 补口子：恢复用户字段 setter、`toJson/fromJson` 覆盖 12 字段、服务端字段（id/时间戳）写入口子（`friend class DataBase`）、`touch()`
2. LinkedList 补齐：`forEach` / `find` / `clear` / `size`；**删除拷贝**（三法则）
3. Trie 修缮：`next[128]`→`[256]`、下标统一 `unsigned char`、Node 递归析构、删拷贝
4. DataBase 核心：`std::mutex`、`ItemQuery/SortField/ListResult`、CRUD 方法（**listItems 含 Top-N 堆，从 Router.cpp 挪入**）、save 原子写
5. Router 收口：调用 `db.listItems` 替换本地堆逻辑；include 卫生（§5.3）
6. HashTable 实现并接入 getItem/updateItem/deleteItem（id → `Item*` 索引）
7. 撤销/重做（UndoStack）、推荐（Recommender）
8. 杂项：确认 `.gitignore` 覆盖 `data/`；`test_api.sh` 字段适配

### 3.3 已定型、不要重新讨论的设计决策

| 决策 | 内容 | 理由速查 |
|---|---|---|
| 分层边界 | 过滤/排序/分页等**数据语义**在 DataBase；参数解析/状态码/JSON 形状等**协议语义**在 Router | 锁边界（§5.2）+ 可测试性（§12） |
| listItems 交接 | 返回 `ListResult{total, vector<const Item*>}`（领域对象），Router 负责序列化 | db 接口不含 json 类型，依赖图干净（§5.3） |
| id 管理 | 服务端 `next_id` 分配器；加载时 `next_id = max(文件值, max_id+1)`；撤销重插用旧 id | 防重发、防手工 JSON 缺字段 |
| 判等 | `Item::operator==` 按 id；内容哈希摘要方案已否决 | 身份走 id 即够，规模小 |
| Top-N | 淘汰堆：堆顶=当前最差；比较器必须全序（同值按 id 裁决，score=0 永远最后） | O(n log k)；分页不重不漏的前提 |
| 堆容量 | `min(offset + limit, items.size())` | offset 无上界，防恶意参数撑爆分配 |
| 错误通道 | 服务层一律返回值（`bool`/指针/`optional`），不抛异常；异常仅用于库边界与进程兜底 | 失败通道数量 = 真实失败模式数量 |
| load/save 失败 | load 失败 → 日志 + 退出进程（fail-fast）；save 失败 → 日志 + 该请求回 500，进程继续 | save 半途失败绝不退出服务器 |
| PUT 语义 | 仅 4 个用户字段可改（progress/score/comment/tags），无 null 语义，空串/0 表空 | Item.h 顶部注释的编辑性约定 |
| 模板 vs 具体 | LinkedList/Heap 模板；HashTable `template<typename V>` 键固定 int；Trie/Item 具体 | 各取所需（§9） |

---

## 4. 目录结构

```text
MeTrace-Server/
├── CMakeLists.txt               # FetchContent 依赖 + GLOB src/*.cpp + 产物 build/bin/server
├── AGENT.md / README.md
├── include/
│   ├── core/                    # 手写数据结构与数据模型（不 include httplib；除 Item.h 外不 include json）
│   │   ├── Item.h               # ✓ 条目模型（字段以此文件为权威）
│   │   ├── Tag.h                # ✓ typedef std::string Tag
│   │   ├── LinkedList.h         # ◐ insert/remove 已有；待 forEach/find/clear/size/删拷贝
│   │   ├── Trie.h               # ◐ 三操作已有；待 256/unsigned char/Node 析构/删拷贝
│   │   ├── Heap.h               # ✓ 固定容量堆（Top-N 淘汰语义）
│   │   ├── HashTable.h          # ○ 空壳：id → Item* 索引（§9.5）
│   │   ├── UndoStack.h          # ○ 待实现
│   │   └── Recommender.h        # ○ 待实现
│   ├── service/
│   │   └── DataBase.h           # ◐ 成员齐；待 mutex + ItemQuery/ListResult + CRUD
│   └── http/
│       └── Router.h             # ◐ registerRoutes 声明（可改前向声明瘦身，§5.3）
├── src/                         # 与 include/ 一一对应 + main.cpp
│   ├── main.cpp                 # ✓ 参数解析 + load/save
│   ├── core/{Item,Trie}.cpp     # ◐
│   ├── service/DataBase.cpp     # ◐ load 已有；save/CRUD 待完善
│   └── http/Router.cpp          # ◐ 堆逻辑待挪回 DataBase（§9.4）
├── scripts/{build.sh,run.sh}
├── tests/
│   ├── test_api.sh              # ✓ 待按新字段适配（§12）
│   └── test_<结构>.cpp          # 计划：简单 assert 程序
└── data/metrace.json            # 运行时生成（确认已 gitignore）
```

图例：✓ 基本完成 / ◐ 部分 / ○ 未开始。铁律：头文件一律放 `include/`，实现一律放 `src/`，文件名与类名一致（大驼峰）；**模板类的实现整体放头文件**（LinkedList/Heap/HashTable 同模式）。

---

## 5. 架构设计

### 5.1 三层结构与职责

```text
客户端 (GUI) ──HTTP+JSON──▶ HTTP 层 (http/Router)     协议语义：解析、校验、调 db、序列化
                              │
                              ▼
                          业务层 (service/DataBase)   数据语义：持有结构、CRUD、过滤排序分页、
                              │                        撤销重做、推荐、加载保存（全部在锁内）
                              ▼
                          结构层 (core/)              手写数据结构与模型（无 httplib、无业务）
                              ▼
                          data/metrace.json           唯一持久化副本
```

判别一段代码属于哪层的问题：**"如果这个服务不再是 HTTP 服务，这段代码还活着吗？"** 活着 → DataBase；死掉 → Router。

| 层 | 允许 | 禁止 |
|---|---|---|
| HTTP | 解析请求、校验参数（400）、调用 DataBase、序列化响应（调 `toJson`）、异常兜底转 500 | 直接操作链表/堆/哈希、读写文件、实现过滤排序逻辑 |
| Service | 持有并同步全部数据结构、锁、load/save、业务规则、调用算法 | include httplib、接口出现 json 类型 |
| Core | 数据结构与算法、Item/Tag 模型与序列化 | include httplib、关心 HTTP 状态码 |

### 5.2 锁边界 = 分层边界（本架构最重要的不变量）

httplib 用线程池并发处理请求，而链表/堆/哈希表都不是线程安全的。**`DataBase` 持一把 `std::mutex`，所有公有方法进入即 `lock_guard`；写方法内"改内存 + save()"整个在锁内执行**。由此推出：任何对数据结构的遍历/修改都必须发生在某个加锁的 db 方法内部——这就是"查询逻辑必须在 listItems 里而不是 Router 里"的物理原因。Router 永远不直接接触任何数据结构。

### 5.3 依赖与 include 卫生

依赖方向必须保持无环、单向：

```text
main.cpp → http/Router.h → service/DataBase.h → core/*.h → <标准库/第三方>
```

1. **IWYU（include what you use）**：每个文件为自己用到的东西显式包含，不搭传递 include 的便车（例：Router.cpp 用 json 就自己 include，不能靠 Router.h 漏下来）；
2. **头文件尽量前向声明**：签名里只有引用/指针时不需要完整类型。目标形态：`Router.h` 仅含 `namespace httplib { class Server; }` 与 `namespace metrace::service { class DataBase; }` 两行前向声明；`DataBase.h` 不 include json（接口用 `ListResult`/`Item*`，不需要 json 类型）；
3. **core 保持纯净**：手写结构（LinkedList/Trie/Heap/HashTable）头文件不 include nlohmann/json；json 只经 Item.h 进入 core；
4. **类型沉降**：跨层共享的类型住在依赖链更下游的那层（Item 在 core，ItemQuery/ListResult 在 service，响应拼装细节只在 Router.cpp）；
5. `#pragma once` 永远是头文件第一行。

---

## 6. 数据存储设计

### 6.1 文件格式

```json
{
  "next_id": 42,
  "items": [ { "id": 1, "type": "book", "...": "..." } ],
  "tags":  [ "科幻", "中国文学" ]
}
```

（tags 为字符串数组——Tag 无独立结构，见 §7.3。）

### 6.2 load（fail-fast，对外承诺绝不抛出）

- 文件不存在 → 空库正常返回；存在但打不开 → 真错误；
- 解析/字段错误：内部 `catch (const nlohmann::json::exception&)` + 兜底 `catch (const std::exception&)`，打日志（含 `e.what()`，parse_error 自带出错位置）返回 false；
- `next_id = max(data.value("next_id", 0), 已加载条目 max_id + 1)`；
- 重载前先 `clear()` 旧内容（LinkedList 需补此方法）；
- `main` 拿到 false → 打印后 `return 1`。**不许吞错当空库跑**：空库第一次 save 会把可能可修复的数据文件覆盖掉；
- 可选加固：先整体 `fromJson` 到临时 vector 全部验证，再统一入链表，避免半加载状态。

### 6.3 save（运行期失败不退出）

1. 拼 json：`next_id` + items + tags（`forEach` 遍历序列化）；
2. 写**同目录**临时文件 `metrace.json.tmp`：`dump(4)`、`flush()`、**检查流状态**（磁盘满/配额此时才暴露，`operator<<` 不报错）；
3. `std::filesystem::rename`（用 `error_code` 重载，不抛）原子替换；任一步失败：删 tmp、`std::cerr` 打 `[save] 阶段: 路径 (ec.message())，原文件未改动`、返回 false → Router 回 500；
4. 两个失败分支日志要可区分（"写入失败" vs "rename 失败"）；rename 失败时 tmp 已写好、原文件未动，删 tmp 是清理；
5. 已接受的简化（可写进报告）：save 失败瞬间内存与磁盘分叉，直到下次成功 save 弥合。

### 6.4 生命周期

`main` 中：`DataBase db;` 声明在 `httplib::Server` **之前**（栈析构顺序相反，杜绝"服务还在跑、库已析构"），`if (!db.load(path)) return 1;`，再 `registerRoutes(server, db)` 传引用。带参构造函数已废弃，统一"默认构造 + load"。

---

## 7. 数据模型

### 7.1 Item（以 `include/core/Item.h` 为准）

| 字段 | C++ | JSON | 约定 |
|---|---|---|---|
| `id` | `int` | number | 服务端分配，只读，删除后不复用 |
| `type` | `std::string` | string | `book`/`movie`/`music`；`kItemTypeName[4]` 映射表含 `"null"` 占位（仅内部默认值，JSON 中不出现） |
| `title` | `string` | string | 必填 ≤200 字符 |
| `author` | `string` | string | 空串=未知 |
| `description` | `string` | string | 空串=无 |
| `date` | `string` | string | 推荐 `YYYY-MM-DD`（**字符串字典序=时间序**，排序可直接比较）；空串=未知 |
| `progress` | `double` | number | 0~1，默认 0 |
| `score` | `int` | number | **1~100，0 = 未评分**（此前文档写 1~10，已按 Item.h 修正） |
| `comment` | `string` | string | 空串=无 |
| `tags` | `vector<Tag>` | string[] | 名称数组，去重、按名称排序，≤20 个 |
| `created_at` / `updated_at` | `string` | string | `YYYY-MM-DD HH:MM:SS`（UTC），服务端管理 |

约定：

- **JSON 中不出现 null**，一切"未知/空"用空串或 0 表达；
- `operator==` 按 **id** 判等（全项目唯一身份判据）；
- 可编辑字段仅 4 个：`progress/score/comment/tags`（PUT 白名单）；`type/title/author/description/date` 创建后只读；
- 服务端字段写入口子：`friend class DataBase`（或等价工厂）；更新刷新 `updated_at` 封装成 `touch()`；
- 访问器风格：getter 标量按值、string/vector 按 `const&`、全部 `[[nodiscard]]` + 尾部 const；setter 一律 `void`（校验归 Router，setter 只做无脑写入），`setTags(std::vector<Tag>)` 按值 + `std::move`。

### 7.2 序列化

`toJson/fromJson` 覆盖**全部 12 字段**。已知历史 bug 勿复发：`j["description"] = j["description"]` 自赋值、`tags` 误读 `j["comment"]`、type 必须经 `kItemTypeName` 映射。

### 7.3 Tag

`typedef std::string Tag`。全局标签表是 `LinkedList<Tag>` 的名字列表：打标签时自动创建不存在的名字；删除条目不级联删标签（孤儿标签是可接受的简化）。标签接口**无 id 字段**（见 §8.2），因此文件里 tags 存字符串数组。

---

## 8. HTTP API 规范

### 8.1 通用约定

- 业务接口统一 `/api` 前缀；`/` 与 `/ping` 不加；请求/响应 `application/json; charset=utf-8`（`/` 例外，返回 text/plain）
- 时间 `YYYY-MM-DD HH:MM:SS`（UTC）；列表接口 `limit`（默认 20，上限 100）+ `offset`（默认 0）
- `total` = **满足过滤条件的总条数**（分页前统计，不受分页影响）
- 错误体 `{"error":"..."}`；状态码即语义：200/201/204/400（参数非法）/404（不存在或无此路由）/500（服务端异常，含 save 失败）
- ⚠️ httplib 陷阱：`set_error_handler` 对一切 ≥400 回调且**不清空 body**，兜底 404 必须先 `if (!res.body.empty()) return;`；挂 `set_exception_handler` 把漏网异常统一转 500；路径参数路由用正则 `R"(/api/items/(\d+))"`，`req.matches[1]` 取捕获组；不匹配（如 `/api/items/abc`）→ 404 属设计行为；请求体解析用非抛出版本 `json::parse(body, nullptr, false)` + `is_discarded()`

### 8.2 接口列表

| 方法 | 路径 | 说明 | 参数/请求体 | 响应 |
|---|---|---|---|---|
| GET | `/` | 纯文本探测 | — | `MeTrace-Server is running`（text/plain） |
| GET | `/ping` | 健康检查 | — | `{"status":"ok",...}` |
| GET | `/api/items` | 条目列表 | `?type=&tag=&sort=&limit=&offset=` | `{"total":N,"items":[Item,...]}` |
| POST | `/api/items` | 新增条目 | 创建字段（type、title 必填） | 201 + Item |
| GET | `/api/items/{id}` | 单个条目 | — | Item |
| PUT | `/api/items/{id}` | 部分更新 | **仅** progress/score/comment/tags，缺省=不改 | Item |
| DELETE | `/api/items/{id}` | 删除条目 | — | 204 |
| GET | `/api/tags` | 标签列表 | `?limit=&offset=` | `{"total":N,"items":["科幻",...]}`（**名字数组，无 id**） |
| POST | `/api/tags` | 新增标签 | `{"name":"科幻"}`，同名幂等 | 201 + `{"name":"科幻"}` |
| POST | `/api/undo` / `/api/redo` | 撤销/重做 | — | `{"success":true,"action":"..."}` |
| GET | `/api/recommend` | 相似推荐 | `?item_id=&topN=` | `{"items":[Item,...]}` |
| GET | `/api/search` | 前缀搜索（计划） | `?q=&type=&limit=&offset=` | 同列表 |
| GET | `/api/stats` | 统计聚合（可选） | `?type=` | `{total,by_type,score_dist}` |

### 8.3 参数细则（GET /api/items 为范式，其余列表接口同构）

| 参数 | 规则 |
|---|---|
| `type` | 白名单 book/movie/movie，非法 → 400 |
| `tag` | 任意字符串，按名精确匹配，缺省不过滤 |
| `sort` | `[-]created_at/score/date/title`，默认 `-created_at`；`-` 前缀=倒序；非法 → 400 |
| `limit` | 非数字 → 400；越界 → **静默截断**到 1~100（枚举非法报错、数值越界宽容，是刻意的不对称） |
| `offset` | 非数字 → 400；负数截断到 0；无上界但堆容量有防护（§9.4） |

实现要点：整数解析用 `std::from_chars`（严格、不抛、拒绝 `"20x"`），不用 `stoi`；校验产物装入 `ItemQuery`；未知参数忽略、空值视同未提供。`ItemQuery/SortField/ListResult` 定义在 `DataBase.h`（类型沉降，§5.3）。

### 8.4 撤销/重做语义

进程内存双栈、不分会话、重启清空；仅写操作入栈（POST/PUT/DELETE items、POST tags）；`Action{kind,itemId,before,after,tagName,description}` 记录反向执行所需快照（Update 记 before/after，Create 记 after，Delete 记 before）；撤销 Create=删除、撤销 Update=恢复 before、撤销 Delete=重插 before；栈空 → 400 `nothing to undo`；容量 100 丢最旧；新写操作清空 redo 栈；撤销/重做本身不入栈。

### 8.5 推荐语义

相似度 = `3×共同标签数 + 2×同作者 + 1×同类型`，同分时评分高者优先、未评分（0）最后；实现可遍历打分 O(n·k) 或邻接表图 O(V+E) 二选一；`item_id` 缺省返回高分榜（Heap 取 TopN）；响应不含条目自身。

---

## 9. 核心数据结构与算法

### 9.1 LinkedList（模板，主存储）

- 已有：头插 insert、remove（prev/cur 双指针，按 `operator==` 即按 id）
- **待补**：`forEach(const std::function<void(const T&)>&) const`（只读遍历，save/列表/推荐全靠它）；`T* find(const T&)`（返回可变指针，PUT 改值用；键字段 id 只读的纪律由 Item 的访问器保证）；`clear()`（load 重载前清空，析构复用）；`size_t size()`（insert/remove 时 O(1) 维护 count_，供堆容量钳制与 total 校验）
- **三法则**：自定义析构已存在，必须 `= delete` 拷贝构造与拷贝赋值
- 纪律：载荷（data）经 `T*` 开放读写；结构变更只走成员函数；**遍历中禁止 insert/remove**（需要就先收集后处理）；辅助结构（HashTable）持有的指针必须与结构变更**同锁同步**

### 9.2 Trie（前缀树，search 用）

- `Node{is_word, cnt_next, next[128]}` → **改 `next[256]`**：中文 UTF-8 字节 ≥0x80，128 会越界；所有下标统一 `unsigned char`（char 有符号，负下标是 UB，编译警告 `-Wchar-subscripts` 即此问题）
- `removeRec` 递归方案已定：后序递归返回"节点是否变空"，父层负责 delete + 断链 + `--cnt_next`；head 是值成员永不删
- **Node 递归析构**：`~Node(){ for (auto c : next) if (c) delete c; }`，`~Trie() = default` 即可。递归深度 = 最长键字节数（≤约 600），栈安全；勿改显式栈（递归的隐式栈只占 O(深度)，显式栈积压未访问兄弟，内存反而差）
- Trie 与 Node 均**删除拷贝**；`operator[]` 的"自动建节点"语义只允许 insert 使用，query/remove 走判空分支（否则删不存在的词会凭空建节点）

### 9.3 Heap（固定容量堆，已实现）

- `template<typename T>` + 构造注入 `std::function<bool(const T&,const T&)> comp`（comp(a,b)=true 表示 a 更靠堆顶）；`explicit` 构造；`new T[capacity]{}` / `delete[]` 配对；拷贝已删（三法则）
- `bool push`：未满→收录返回 true；满且更优→顶掉堆顶返回 true；满且不优→false（**设计内常态，不抛异常**——异常只留给 bad_alloc 这类系统灾难，由 main 顶层兜底）
- `T pop()`：末元素补根再下沉；空堆 pop 是调用方违约，`assert` 拦截；`top()/size()/empty()` 伴生
- 命名即文档：sift=逐层比较条件推进，shift=无条件整体平移，勿混用
- 构造时容量 `assert(>0)`（容量 0 几乎必是调用方笔误，出生时拦截）

### 9.4 listItems 的 Top-N（淘汰堆用法，住在 DataBase.cpp）

```text
上锁 → forEach: 匹配过滤则 total++ 且参与堆竞争 → 弹出全部（最差在前）→ reverse → 跳过 offset 取至多 limit → 解锁返回
```

- **淘汰堆方向**：堆顶 = 当前 k 个候选中最差者，新元素只与堆顶比较——这是 O(n log k) 的机关；`listItems` 循环体因此缩成一行 `push`
- **比较器必须全序**：字段值同 → 按 id 裁决（**分页不重不漏的硬前提**，否则跨页重复/丢失）；score=0 无论方向永远最后
- **容量 = min(offset+limit, items.size())**：offset 无上界，防恶意参数巨额分配
- 高分榜（recommend 冷启动）复用 Heap，comp 方向取正即可

### 9.5 HashTable（待实现，`template<typename V>` 键固定 int）

- 链地址法：桶为 Entry 单链表；除法散列 `key % 桶数`（桶数取素数，初始 17）；负载因子 >0.75 → rehash 到约两倍素数（**摘下重挂**，不是复制后删）
- 接口：`V* find(int)` / `void insert(int, V)`（已存在则覆盖）/ `bool erase(int)` / `size()`
- **不拥有值**：析构逐桶 delete Entry 但绝不 delete 值；存的是 `Item*`（指向链表节点内的 data）——存拷贝无用（PUT 会改到副本），存 Node* 不可能（private 类型）
- 与链表同步在 DataBase 锁内：插入同挂、删除同摘；漏同步 = 悬垂指针
- 落地策略：先写 O(n) 链表版 CRUD 跑通 HTTP，HashTable 作为**内部加速器**后替换，对外签名不变，`test_api.sh` 全绿作回归验证

### 9.6 UndoStack / Recommender（待实现）

规格见 §8.4 / §8.5。UndoStack 为双栈，`push/undo/redo` 均摊 O(1)，容量 100，超出丢最旧。结构 → 真实调用方对应表（答辩必问）：

`LinkedList` → 全部 CRUD；`Heap` → listItems / recommend；`HashTable` → getItem/update/delete；`Trie` → search；`UndoStack` → undo/redo；`Recommender` → recommend。**只写不用的结构不算数**。

---

## 10. 开发规范

### 10.1 C++ 惯例（已确立，照做）

- 类名/文件名大驼峰，函数小驼峰，变量小写下划线；命名空间 `metrace::core/service/http`；禁 `using namespace std;`
- **三法则**：任何手动 `new`/`delete` 资源的类（LinkedList/Trie/Heap/HashTable），析构之外必须删除拷贝；判别法——"这个类的指针一浅拷贝会怎样"
- **explicit**：单参（或单必选参）构造默认加；**[[nodiscard]]**：getter 与不可忽略的返回值加
- 返回类型：getter 标量按值、string/vector 按 const&；setter 一律 void；**失败通道数量 = 真实失败模式数量**（0 失败→void，1 失败→bool/optional/指针，多种且需区分→带数据的结果类型）
- 异常边界：库（nlohmann/httplib/std::stoi）会抛 → Router 用非抛出 API、load 内 catch 收编、main 顶层兜底；服务层自身签名不抛
- 原始字符串 `R"(...)"`：括号是语法框架，正则与内嵌引号的 JSON 用它，普通文本不用
- 成员初始化顺序 = 声明顺序；初始化列表书写顺序与声明顺序保持一致
- 注释中文，核心结构必须注明复杂度；Doxygen 风格 `///`

### 10.2 Git 与日志

- 提交信息 `[模块] 简要描述`（如 `[core] 实现 HashTable 链地址法与扩容`）；修 bug 与架构调整分开提交
- 日志统一 `[模块] 动作: 细节 (原因)` 前缀，一行一事，失败分支可区分

### 10.3 码量纪律

评分看结构与算法，不看分层花样：删得动的抽象都删掉；不引 gtest、不拆 Models.h、不做 pimpl；`std::vector` 可作底层容器，但节点串接、sift、散列扩容、Trie 节点、递归释放必须手写。

---

## 11. 构建与运行

```bash
./scripts/build.sh                        # = cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./scripts/run.sh                          # 127.0.0.1:8000 + ./data/metrace.json
./scripts/run.sh --port 9000 --db /tmp/demo.json
./tests/test_api.sh http://127.0.0.1:8000 # 需服务端已启动
```

- CMake：GLOB_RECURSE 收集 `src/*.cpp`（CONFIGURE_DEPENDS，新增文件免改构建；Makefile/Ninja 生成器下生效）；`-Wall -Wextra -Wpedantic`，**目标零警告**；Release 下 LTO
- 参数优先级：命令行 > 环境变量（`METRACE_HOST/PORT/DB`）> 默认值；`--db` 父目录自动创建

---

## 12. 测试策略

- **结构单测**：每个 core 结构一个 `tests/test_<结构>.cpp`（普通 main + assert）。Heap 用例：容量 3 降序灌 10 → 弹出序 {10,9,8}；同值按 id 裁决；`Heap b = a;` 应**编译失败**（禁拷贝生效的证明）。LinkedList：空表/头节点/不存在 id 边界
- **db 可直接单测**：listItems 等 db 方法不依赖 HTTP 即可调用验证（分层红利）
- **接口测试**：`test_api.sh` 适配新字段（creator→author、review→comment、year→date、score 为 int 1~100、tags 无 id）
- 压测（阶段 4）：1000 条下 listItems / recommend 响应时间

---

## 13. 开发任务清单

### 阶段 0：清理与恢复编译 ✅（基本完成）

- [x] 移除 SQLiteCpp；CMake 仅 httplib + nlohmann_json
- [x] 全部头文件 `#pragma once`
- [x] main.cpp：参数解析 + load fail-fast + 声明顺序（db 先于 server）
- [ ] 收尾：确认 `.gitignore` 含 `data/`

### 阶段 1：数据层（10.1~10.5，进行中）

- [ ] Item：恢复 setter、toJson/fromJson 全字段、friend DataBase、touch()
- [ ] LinkedList：forEach / find / clear / size + 删拷贝 + `tests/test_linkedlist.cpp`
- [ ] Trie：next[256] + unsigned char + Node 递归析构 + 删拷贝；清零编译警告
- [ ] DataBase：mutex；save 原子写（§6.3）；next_id 兜底初始化；load 加固（§6.2）
- [ ] 验收：手工构造 JSON，load → 改内存 → save 回读一致

### 阶段 2：HTTP CRUD（10.6~10.9）

- [ ] `ItemQuery/SortField/ListResult` 落入 DataBase.h；实现 listItems（**Top-N 堆从 Router.cpp 挪入**，§9.4）/ getItem / createItem / updateItem / deleteItem / tags 两方法（save 在锁内）
- [ ] Router：/api/items 五路由 + /api/tags 两路由 + 校验（§8.3）+ error/exception handler
- [ ] include 卫生：Router.h 前向声明；DataBase.h 去 json；Router.cpp 自带 json
- [ ] `test_api.sh` 字段适配，全部断言通过

### 阶段 3：算法功能（10.10~10.14，两人并行）

- [ ] A 线：UndoStack + undo/redo 路由 + 写操作入栈
- [ ] B 线：Recommender（相似 + 高分榜复用 Heap）+ /api/recommend；可选 Trie+search / HashTable 索引接入
- [ ] 各自 `tests/test_*.cpp`；能指着代码讲结构与复杂度

### 阶段 4：完善与验收（10.15~10.18）

- [ ] 50~100 条演示数据 `scripts/seed_data.json`；1000 条压测
- [ ] 文档核对（README/本文档与代码一致）；演示视频与讲稿；人均码量核查

---

## 14. 两人分工

| 成员 | 负责 | 结构 |
|---|---|---|
| A | 存储侧：链表、JSON 序列化与持久化、撤销重做 | LinkedList、UndoStack（+可选 HashTable） |
| B | 算法侧：Top-N/推荐/排序、（可选）搜索与索引 | Heap、Recommender（+可选 Trie/HashTable） |

HTTP 层共同维护。每人必须能独立讲清自己结构的定义、操作实现与复杂度。

---

## 15. 验收标准与演示

- 服务独立启动、全部接口可用、数据重启不丢；`test_api.sh` 全绿
- 手写结构有**真实调用方**（对应表见 §9.6），能现场解释与复杂度分析
- 撤销/重做、推荐可演示；讲解顺序建议：链表节点 → 淘汰堆与全序比较器 → 双栈 → 推荐
- 能回答两个高频追问："为什么查询逻辑在 db 不在 Router"（锁边界）；"为什么不用平衡树做排序索引"（过滤组合使索引失效 + 规模账：千条排序微秒级）

---

## 16. 注意事项

1. 服务端纯 C++，不用 Python 中间层；**主存储是内存结构，JSON 文件是唯一持久化副本**，除 load/save 外任何代码不直接读写该文件
2. **锁内做一切数据操作**；写操作"改内存 + save"在同一临界区
3. **比较器必须全序**（id 兜底），否则堆和分页都会悄悄坏
4. 新头文件第一行 `#pragma once`；include 不搭便车；core 结构头文件不进 json
5. 三法则：拥有裸指针资源的类必删拷贝
6. 写操作落盘必须原子写（tmp + rename），这是硬性要求
7. 文档先于代码：改接口先改本文档 §8；字段以 §7 为准
8. 保持零警告（`-Wall -Wextra -Wpedantic` 一直开着，新警告一冒头就修）

---

## 17. 后续可能的开发方向

- 条目只读信息（标题/作者/日期/简介）从外部数据源（如 NeoDB）导入，丰富用户上传之外的创建途径
- 数据量或并发访问显著增长时，可将存储层替换为嵌入式数据库，上层 HTTP 与业务接口保持不变
