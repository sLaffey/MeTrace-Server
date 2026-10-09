# MeTrace-Server

> 书影音迹 MeTrace —— 个人书影音追踪与智能推荐系统的服务端
>
> 《计算机科学与工程程序设计实践》课程设计项目。C/S 架构，服务端纯 C++ 实现，客户端 GUI 通过 HTTP + JSON 通信。

一个用来管理"书 / 影 / 音"收藏记录的 HTTP 服务：记录条目、打分、写短评、打标签，并在此之上提供相似推荐。数据以内存链表为主存，持久化采用单个 JSON 文件——存储方案为课设特意简化，重心放在数据结构与算法上（链表、堆、Trie、哈希表、推荐算法等均为手写实现）。撤销/重做由客户端 GUI 本地实现（双栈在客户端），服务端只提供普通 CRUD。

---

## 目录

- [特性](#特性)
- [技术栈](#技术栈)
- [快速开始](#快速开始)
- [配置](#配置)
- [API 文档](#api-文档)
- [数据存储](#数据存储)
- [架构与目录结构](#架构与目录结构)
- [数据结构与算法](#数据结构与算法)
- [开发进度](#开发进度)
- [常见问题](#常见问题)
- [后续可能的开发方向](#后续可能的开发方向)

---

## 特性

- 条目的增删改查，覆盖书 / 影 / 音与"未分类"（`uncategorized`）四种类型取值
- 部分更新（PUT）：只传需要改的字段；除服务端自动管理的 `id` 与时间戳外，其余 9 个用户字段都可修改
- 标签系统：打标签时自动创建不存在的标签，条目与标签多对多关联
- 组合过滤 + 排序 + 分页：按类型、标签筛选，按评分 / 日期 / 创建时间 / 标题排序；排序取 Top-N 采用手写淘汰堆，O(n log k)
- 完整的参数校验与语义化状态码（400 / 404 / 500）
- 手写数据结构承担全部业务：**单链表**作主存储、**固定容量二叉堆**做 Top-N 排序、**Trie** 作全局标签表、**哈希表**作 id 索引、**推荐算法**基于标签重叠/作者/类型相似度
- 撤销/重做归客户端：服务端不设 `/api/undo`、`/api/redo`，只保证写请求原子并回传完整 after 态，供客户端双栈回放
- 原子落盘：写操作先写临时文件再 rename，中途崩溃不会损坏数据文件
- 线程安全：httplib 线程池并发处理请求，数据访问通过单一互斥锁串行化

---

## 技术栈

| 类别 | 选型 | 版本 |
|---|---|---|
| 语言 | C++17 | — |
| 构建 | CMake | ≥ 3.22.1 |
| HTTP | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | v0.57.1 |
| JSON | [nlohmann/json](https://github.com/nlohmann/json) | v3.12.0 |
| 持久化 | 本地 JSON 单文件 | — |

依赖由 CMake 的 `FetchContent` 直接拉取源码并锁定版本，**不需要系统预装**，也不需要 `third_party/` 目录。

---

## 快速开始

### 环境要求

- CMake ≥ 3.22.1
- 支持 C++17 的编译器（GCC ≥ 9 / Clang ≥ 10 / MSVC 2019+）
- 首次构建需要联网（拉取依赖）
- 运行测试需要 `curl` 与 `jq`

### 构建

```bash
./scripts/build.sh
```

等价于：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

产物固定在 `build/bin/server`。

### 运行

```bash
./scripts/run.sh
# MeTrace-Server 已启动: http://127.0.0.1:8000  (db: ./data/metrace.json)
```

也可以直接带参数启动：

```bash
./build/bin/server --host 0.0.0.0 --port 9000 --db /tmp/demo.json
./build/bin/server --help
```

`--db` 指向的父目录不存在时会自动创建；文件不存在则按空库启动。

### 试一试

```bash
# 健康检查
curl http://127.0.0.1:8000/ping

# 新增一个条目（标签"科幻"不存在会自动创建）
curl -X POST http://127.0.0.1:8000/api/items \
  -H 'Content-Type: application/json' \
  -d '{"type":"book","title":"三体","author":"刘慈欣","date":"2006-01-01",
       "progress":0.5,"score":90,"comment":"震撼","tags":["科幻","中国文学"]}'

# 查列表：只看书，按评分倒序，第一页
curl 'http://127.0.0.1:8000/api/items?type=book&sort=-score&limit=10'

# 改一条记录（只传要改的字段）
curl -X PUT http://127.0.0.1:8000/api/items/1 \
  -H 'Content-Type: application/json' \
  -d '{"progress":1,"score":98}'

# 和《三体》相似的条目
curl 'http://127.0.0.1:8000/api/recommend?item_id=1&topN=5'

# 删除
curl -i -X DELETE http://127.0.0.1:8000/api/items/1
```

### 接口测试

`tests/test.sh` 是自包含的回归测试：用 `tests/metrace.json`（21 条种子数据，含 1 条"未分类"，已纳入版本控制）在临时目录里起一个服务端，跑完断言后自动关停，无需手动准备环境；覆盖接口行为、种子加载语义（幽灵标签丢弃、tags 排序去重、`uncategorized` 取值）与启动行为（文件缺失按空库启动；文件损坏或字段非法一律 fail-fast 拒绝启动，且不改写数据文件），共 190 项断言。

```bash
./tests/test.sh                                 # 依赖 curl 与 jq
./tests/test.sh --url http://127.0.0.1:8000     # 只对已运行的服务端测试（跳过种子数据断言）
./tests/test.sh --server ./build/bin/server --port 18000
```

---

## 配置

优先级：**命令行参数 > 环境变量 > 默认值**。

| 命令行参数 | 环境变量 | 默认值 | 说明 |
|---|---|---|---|
| `--host <addr>` | `METRACE_HOST` | `127.0.0.1` | 监听地址 |
| `--port <port>` | `METRACE_PORT` | `8000` | 监听端口（1~65535） |
| `--db <path>` | `METRACE_DB` | `./data/metrace.json` | JSON 存储文件路径 |
| `--help` / `-h` | — | — | 打印用法 |

```bash
METRACE_PORT=9000 ./scripts/run.sh              # 用环境变量
./scripts/run.sh --port 9000                    # 用命令行（优先级更高）
```

---

## API 文档

### 通用约定

- 基础 URL：`http://127.0.0.1:8000`
- 所有业务接口统一 `/api` 前缀；`/` 与 `/ping` 不加前缀
- 请求与响应均为 `application/json; charset=utf-8`
- ID 为 JSON 数字；服务器管理的时间字段（`created_at`/`updated_at`）为 Unix **秒级时间戳**（UTC 整数），格式化展示由客户端负责；条目 `date` 为 `YYYY-MM-DD` 字符串
- 列表接口支持 `limit`（默认 20，上限 100）与 `offset`（默认 0）
- "未知 / 为空"统一用空串或 `0` 表达（未评分时 `score` 为 `0`），**响应中不出现 `null`**

> 响应中的 JSON 键按字母序排列（nlohmann/json 默认行为），本文档示例为便于阅读按业务字段顺序书写，键序不影响解析。

**错误响应**

```json
{ "error": "field 'title' is required and must be a non-empty string" }
```

HTTP 状态码本身即语义，body 中不再重复 `code` 字段：

| 状态码 | 含义 |
|---|---|
| 200 | 成功 |
| 201 | 创建成功（POST） |
| 204 | 删除成功（无 body） |
| 400 | 参数非法（缺必填、枚举取值错误、数值越界、JSON 格式错误、只读字段出现在 PUT 中） |
| 404 | 资源不存在，或路径未匹配任何路由 |
| 500 | 服务端异常（含数据落盘失败） |

### 接口一览

| 方法 | 路径 | 说明 | 参数 / 请求体 | 响应 |
|---|---|---|---|---|
| GET | `/` | 纯文本存活探测 | — | `MeTrace-Server is running` |
| GET | `/ping` | 健康检查 | — | `{"status":"ok","service":"MeTrace-Server"}` |
| GET | `/api/items` | 条目列表 | `?type=&tag=&sort=&limit=&offset=` | `{"total":N,"items":[Item,...]}` |
| POST | `/api/items` | 新增条目 | Item 创建字段（`type`、`title` 必填） | `201` + `Item` |
| GET | `/api/items/{id}` | 单个条目 | — | `Item` |
| PUT | `/api/items/{id}` | 部分更新 | 9 个用户字段（`type`…`tags`），缺省=不改 | `Item` |
| DELETE | `/api/items/{id}` | 删除条目 | — | `204`（无 body） |
| GET | `/api/tags` | 标签列表 | `?limit=&offset=` | `{"total":N,"tags":["科幻",...]}` |
| POST | `/api/tags` | 新增标签 | `{"name":"科幻"}`，同名幂等 | `201` + `{"name":"科幻"}` |
| GET | `/api/recommend` | 相似推荐 | `?item_id=&topN=` | `{"items":[Item,...]}` |

> **撤销/重做不在服务端**：双栈由客户端 GUI 实现，服务端不提供对应接口；客户端回放所需的契约（完整 after 态、DELETE 无 body、只读 id）见下文「撤销/重做（客户端实现）」小节。
>
> 计划中：`/api/search`（Trie 前缀搜索）、`/api/stats`（统计聚合），见 [开发进度](#开发进度)。

### 查询参数

| 参数 | 取值 | 说明 |
|---|---|---|
| `type` | `uncategorized` / `book` / `movie` / `music` | 缺省不过滤；`uncategorized` 表示未分类 |
| `tag` | 标签名称 | 按名称精确匹配 |
| `sort` | `created_at` / `score` / `date` / `title`，前缀 `-` 表示倒序 | 默认 `-created_at` |
| `limit` | 1~100 | 默认 20，超出范围会被截断 |
| `offset` | ≥ 0 | 默认 0 |
| `item_id` | 条目 id | `/api/recommend` 缺省时返回全局高分榜（冷启动兜底） |
| `topN` | 正整数，默认 10，上限 50 | 推荐条数 |

取值非法（例如 `type=game`、`sort=unknown`）一律返回 400，而不是静默忽略；`limit` 数值越界静默截断到 1~100，`offset` 为负返回 400。

### 数据模型

**Item**（以 `include/core/Item.h` 为准）

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` | number | 主键，服务端生成，删除后不复用 |
| `type` | string | `uncategorized`（未分类）/ `book` / `movie` / `music`；POST 时必填，未分类可显式传入 |
| `title` | string | 标题，必填，≤ 200 字节（UTF-8 字节数） |
| `author` | string | 作者 / 导演 / 艺术家，空串表示未知 |
| `description` | string | 简介，空串表示无 |
| `date` | string | 发布日期，推荐 `YYYY-MM-DD`，空串表示未知 |
| `progress` | number | 进度比例 `0`~`1`，默认 `0` |
| `score` | number | 评分 `1`~`100` 的整数，**`0` 表示未评分** |
| `comment` | string | 短评，空串表示无 |
| `tags` | string[] | 标签名称数组，按名称排序，单个条目最多 20 个 |
| `created_at` | number | 创建时间，Unix 秒级时间戳（UTC），只读 |
| `updated_at` | number | 更新时间，Unix 秒级时间戳（UTC），每次 PUT 自动刷新 |

条目字段分为两类：**用户可编辑**（`type`、`title`、`author`、`description`、`date`、`progress`、`score`、`comment`、`tags` 共 9 个，可通过 PUT 修改）与**只读**（`id`、`created_at`、`updated_at`，由服务端自动管理）。

**Tag**

标签即名称字符串（全局唯一，非空且 ≤ 50 字节），无独立 id；标签接口一律按名称操作。

### 示例

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
  "score": 90,
  "comment": "震撼",
  "tags": ["科幻", "中国文学"]
}
```

```json
// 响应 201 Created
{
  "id": 1,
  "type": "book",
  "title": "三体",
  "author": "刘慈欣",
  "description": "地球文明三部曲第一部",
  "date": "2006-01-01",
  "progress": 0.5,
  "score": 90,
  "comment": "震撼",
  "tags": ["中国文学", "科幻"],
  "created_at": 1790848800,
  "updated_at": 1790848800
}
```

说明：

- `tags` 中不存在的标签会被自动创建并写入全局标签表；重复标签自动去重。
- 未提供的可选字段使用默认值（`progress`→`0`、`score`→`0`、字符串→空串、`tags`→`[]`）。
- 响应回传完整记录（含服务端生成的 `id` 与时间戳），便于客户端直接刷新本地缓存。

**PUT /api/items/{id}** 是部分更新，接受全部 9 个用户可编辑字段：

```bash
curl -X PUT http://127.0.0.1:8000/api/items/1 \
  -H 'Content-Type: application/json' \
  -d '{"score": 98}'
# 只改评分，其余字段不变
```

- 出现在请求体里的字段才会被写入，未出现的字段保持原值。
- `tags` 传数组即整体替换，传 `[]` 即清空标签。
- 请求体中出现只读字段（`id`/`created_at`/`updated_at`）或 `null` 一律返回 400——这些字段没有合法的修改途径，静默忽略会掩盖客户端 bug。

**GET /api/items**

```json
{
  "total": 37,
  "items": [
    {
      "id": 1,
      "type": "book",
      "title": "三体",
      "author": "刘慈欣",
      "progress": 1.0,
      "score": 98,
      "tags": ["中国文学", "科幻"],
      "created_at": 1790848800,
      "updated_at": 1790850600
    }
  ]
}
```

`total` 是**满足过滤条件的总条数**（不受分页影响），用于客户端计算页数；`items` 只包含当前页。

**撤销/重做（客户端实现）**

服务端**不提供** `/api/undo`、`/api/redo`，也不持有任何撤销快照；双栈、容量上限（建议 100 丢最旧）、清空 redo 栈等纪律全部归客户端 GUI。客户端回放依赖以下服务端契约：

- `POST /api/items` 返回 `201` + 完整条目，`PUT /api/items/{id}` 返回 `200` + 完整条目 —— 客户端可直接用响应刷新缓存并入栈；
- `DELETE /api/items/{id}` 返回 `204`（无 body）：要支持"撤销删除"，客户端必须在删除前自持完整条目快照；
- **撤销删除 = 以新 `id` 重建**：`id`/`created_at`/`updated_at` 是服务端只读字段，POST 不接受客户端指定 id，因此重建得到新 id 与新时间戳，原 id 不复用；
- 撤销 PUT 时旧值需由客户端自行快照（PUT 是部分更新，只回传 after 态）；
- 撤销"新增条目" = 发 DELETE，条目引用的标签仍留在全局标签表（孤儿标签是可接受的简化），标签没有删除接口。

**GET /api/recommend?item_id=1&topN=5**

返回与指定条目最相似的 TopN 条目（不含自身）。相似度 = `3 × 共同标签数 + 2 × 同作者 + 1 × 同类型`，同分时评分高者优先。`item_id` 缺省时返回全局高分榜。

---

## 数据存储

单个 JSON 文件（默认 `./data/metrace.json`）：

```json
{
  "next_id": 42,
  "items": [ { "id": 1, "type": "book", "title": "三体", "...": "..." } ],
  "tags":  [ "科幻", "中国文学" ]
}
```

- **加载**：启动时全量读入内存，逐条反序列化建链表；文件不存在按空库启动；文件损坏、字段缺失或字段值非法（未知 `type`、`score`/`progress` 越界、`title` 为空或超 200 字节、tag 为空或超 50 字节、`id` 重复等）一律拒绝启动（fail-fast），绝不吞错后用空库覆盖原文件。**不做迁移**：早期版本写入的 `type: "null"` 属于非法值，需要手工改成 `"uncategorized"`。
- **保存**：每个写操作成功后全量覆写；写入是**原子操作**（先写临时文件再 rename），中途崩溃不会损坏数据文件。
- `next_id` 持久化在文件中，删除条目后 ID 不复用。
- 删除该文件即可清空所有数据，下次启动自动从空库开始。

数据规模按个人收藏（几十~几百条）设计，全量加载/保存的性能绰绰有余，也使存储实现保持在一百行以内。

---

## 架构与目录结构

```text
客户端 (GUI)
    │ HTTP + JSON
    ▼
HTTP 层 (src/http/Router.cpp)      路由注册 · 参数校验 · 响应序列化
    ▼
业务层 (service/DataBase)          过滤/排序/分页 · CRUD · 推荐 · 加载保存（加锁）
    ▼
数据结构与算法层 (core/)           LinkedList · Heap · Trie · HashTable · Recommender · Item/Tag
    ▼
本地 JSON 文件 (data/metrace.json)
```

分层职责，禁止跨层：HTTP 层只做协议翻译（解析参数、校验、调 db、序列化响应），不直接操作任何数据结构；业务层接口不出现 JSON 类型；core 层不关心 HTTP。数据查询（过滤/排序/分页）全部是加锁的 DataBase 方法——线程安全边界与分层边界重合。

```text
MeTrace-Server/
├── CMakeLists.txt
├── AGENT.md                     # 开发指南（架构约定、接口规格、数据结构要求与阶段计划）
├── README.md                    # 本文件
├── include/
│   ├── core/                    # 手写数据结构与数据模型（Item / LinkedList / Trie / Heap / HashTable / …）
│   ├── service/
│   │   └── DataBase.h           # 数据库门面：链表 + 标签表 + 加载保存 + CRUD
│   └── http/
│       └── Router.h
├── src/                         # 与 include/ 一一对应的实现 + main.cpp
├── scripts/
│   ├── build.sh
│   └── run.sh
├── tests/
│   ├── test.sh                  # 自包含回归测试（自动起停服务端，curl + jq）
│   └── metrace.json             # 测试种子数据（21 条，含 1 条 uncategorized，已纳入版本控制）
├── data/                        # 运行时生成的 JSON 存储文件（已 gitignore）
└── build/                       # 构建产物，含 FetchContent 缓存的依赖
```

### 线程安全

httplib 默认用线程池并发处理请求，而内存链表不是线程安全的。`DataBase` 持有一把 `std::mutex`，所有公开方法（含保存）进入即加锁，"改内存 + 落盘"在同一临界区内完成——同一时刻只有一个线程操作数据，对这个量级的个人应用足够。

---

## 数据结构与算法

本项目为数据结构与算法课设，以下结构均为手写实现并承担真实业务（非摆设）：

| 结构 / 算法 | 状态 | 关键操作与复杂度 | 用途 |
|---|---|---|---|
| 单链表 `LinkedList` | 已实现 | 头插 O(1)，按 id 查找/删除 O(n) | 全部条目的主存储，所有 CRUD 在链表上进行 |
| 二叉堆 `Heap` | 已实现 | push/pop O(log n)；固定容量，满员自动淘汰最差 | 列表排序与高分榜的 Top-N，O(n log k) |
| 推荐算法 `Recommender` | 计划 | 遍历打分 O(n·k)，或图共同邻居 O(V+E) | 相似推荐；冷启动高分榜复用堆 |
| 前缀树 `Trie` | 已实现 | 插入 / 查询 / 删除 O(L)，有序枚举 | 全局标签表：查重、有序列表、落盘收集；可选接入前缀搜索 |
| 哈希表 `HashTable` | 计划 | 链地址法，平均 O(1)，负载因子 >0.75 扩容 | id → 条目索引，加速单查/改/删（服务端第四个结构） |

Top-N 采用**淘汰堆**：堆顶始终是当前候选中最差者，新元素只与堆顶比较，整趟扫描 O(n log k)；比较器以 id 做全序兜底，保证分页不重不漏。

服务端手写结构为 **LinkedList / Trie / Heap / HashTable 四个**，另加 **Recommender** 算法（HashTable 与 Recommender 待实施，见上表状态）；**撤销/重做的双栈在客户端 GUI**，不计入服务端结构账。

---

## 开发进度

### 阶段 0：清理残留，恢复编译 ✅

- [x] 移除数据库依赖，CMake 仅保留 httplib + nlohmann_json
- [x] 全部头文件加 `#pragma once`；main.cpp 参数解析与加载流程定稿

### 阶段 1：数据层（基本完成）

- [x] Trie 插入/查询/删除（并作为全局标签表）；Heap 固定容量堆
- [x] DataBase::load（标签校验：幽灵标签丢弃）+ `next_id` 兜底 + save 原子落盘
- [x] load 值域校验（`type` 白名单、title/score/progress/tags、id 唯一性、时间戳）：fail-fast、不做迁移，已实现（2026-10-09）
- [x] Item 序列化全字段与服务端字段口子（id/时间戳自动管理）；LinkedList 遍历/查找/计数
- [ ] 互斥锁、写操作锁内落盘（收尾中）

### 阶段 2：HTTP CRUD（进行中）

- [x] `/api/items` 增删改查五路由 + GET `/api/tags`（Top-N 堆在 DataBase 内）
- [x] 写路径收口：统一校验（键白名单/类型/值域/tags 上限）、错误体规范、时间戳刷新与标签注册
- [x] `type` 白名单定为 `uncategorized`（未分类）/`book`/`movie`/`music`，未分类可显式创建；POST 的 `type` 必填（2026-10-08）
- [x] PUT body 解析修复、`limit` 契约截断 1~100（`limit=0` 曾致服务崩溃）、`/` 回 text/plain
- [ ] POST `/api/tags`、错误处理器挂载与收尾
- [x] `tests/test.sh` 自包含回归全绿（190 项，含未分类与加载校验矩阵）；旧 `test_api.sh` 已删除

### 阶段 3：HashTable 索引与推荐（10.10~10.14）

- [ ] `HashTable`（id → `Item*` 索引）接入单查 / 改 / 删
- [ ] `Recommender` + `/api/recommend`
- [ ] 可选：Trie 接入 `/api/search`

> 撤销/重做改由客户端 GUI 实现，服务端不再规划 `UndoStack` 与 `/api/undo`、`/api/redo`（2026-10-08 定）。

### 阶段 4：完善与验收（10.15~10.18）

- [ ] 50~100 条演示数据
- [ ] 性能测试（1000 条数据下的推荐与列表响应时间）
- [ ] 演示视频与讲稿

---

## 常见问题

**首次 `cmake` 卡在下载依赖？**

`FetchContent` 需要访问 GitHub 拉取依赖，缓存在 `build/_deps/`。网络不畅时可以配置代理，或提前在有网的环境构建一次后把 `build/` 目录一起带走。**演示前务必先跑通一次构建，之后不要删除 `build/`**。

**为什么 `build/bin/server` 而不是 `build/server`？**

`CMakeLists.txt` 里设置了 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 统一输出到 `build/bin/`。

**数据文件在哪？**

默认 `./data/metrace.json`（相对于启动时的工作目录）。用 `--db` 可以指定其它位置。删除该文件即可清空所有数据。

**`score` 为 `0` 是什么意思？**

表示"未评分"。有效评分为 1~100 的整数；响应中不使用 `null`。

**`type` 有哪些取值？`uncategorized` 是什么？**

四个合法取值：`book` / `movie` / `music` / `uncategorized`（未分类）。`uncategorized` 由早期的 `"null"` 改名而来（2026-10-08），POST / PUT 可显式传入，查询参数同样可用；POST 时 `type` 仍为必填，没有缺省值。

**数据文件坏了会怎样？**

JSON 解析失败、字段缺失或字段值非法（未知 `type`、`score`/`progress` 越界、`title` 为空、tag 为空或超长、`id` 重复等）时，服务端都会拒绝启动并打印出错位置，而不是当空库继续跑——后者会在第一次保存时把可能还能修复的数据文件覆盖掉。加载**不做迁移**：早期版本写入的 `type: "null"` 需要手工改成 `"uncategorized"`。

**撤销/重做重启后还在吗？**

撤销/重做由客户端本地实现，历史保存在客户端进程内：客户端重启即清空，服务端重启不影响客户端已持有的历史；已写入 JSON 文件的数据不受影响。

**服务端为什么不提供 `/api/undo`？**

按课设的结构盘点（见 [数据结构与算法](#数据结构与算法)），服务端的手写结构是 LinkedList / Trie / Heap / HashTable 四个（HashTable 待实施）；撤销/重做的双栈放在客户端 GUI 侧，那里有真实调用方。服务端只保证写请求原子、回传完整 after 态，并让 `DELETE` 保持 204 无 body。

**`GET /api/items/abc` 为什么返回 404 而不是 400？**

路由用正则 `(\d+)` 匹配数字 ID，`abc` 不匹配任何路由，因此按"路由不存在"返回 404。这与"路径匹配上了但 ID 不合法"是两种情况。

**响应里 `progress` 为什么是 `1.0` 而不是 `1`？**

`progress` 是双精度浮点数，JSON 序列化会保留小数形式。两者数值相等，客户端按数值解析即可。

**中文标签的排序规则？**

标签按名称升序返回，比较的是 UTF-8 字节序，因此 `"中国文学"` 排在 `"科幻"` 前面。展示顺序由客户端决定即可。

---

## 后续可能的开发方向

- 条目只读信息（标题/作者/日期/简介）从外部数据源（如 NeoDB）导入，丰富用户上传之外的创建途径
- 数据量或并发访问显著增长时，可将存储层替换为嵌入式数据库，上层 HTTP 与业务接口保持不变
- 标签改名（`PUT /api/tags/{name}`）：暂缓，基本开发完成后视客户端需要再加；实现与设计取舍见 `AGENT.md` §17

---

## 相关文档

- [`AGENT.md`](AGENT.md) —— 开发指南：架构约定、接口规格、数据结构要求与阶段计划
