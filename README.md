# MeTrace-Server

> 书影音迹 MeTrace —— 个人书影音追踪与智能推荐系统的服务端
>
> 《计算机科学与工程程序设计实践》课程设计项目。C/S 架构，服务端纯 C++ 实现，客户端 GUI 通过 HTTP + JSON 通信。

一个用来管理"书 / 影 / 音"收藏记录的 HTTP 服务：记录条目、打分、写短评、打标签，并在此之上提供相似推荐与撤销/重做。数据以内存链表为主存，持久化采用单个 JSON 文件——存储方案为课设特意简化，重心放在数据结构与算法上（链表、双栈、推荐算法等均为手写实现）。

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

- 条目的增删改查，覆盖书 / 影 / 音三种类型
- 部分更新（PUT）：只传需要改的字段，只允许修改进度、评分、短评、标签四个用户字段
- 标签系统：打标签时自动创建不存在的标签，条目与标签多对多关联
- 组合过滤 + 排序 + 分页：按类型、标签筛选，按评分 / 日期 / 创建时间 / 标题排序
- 完整的参数校验与语义化状态码（400 / 404 / 500）
- 手写数据结构承担全部业务：**单链表**作主存储、**双栈**实现撤销/重做、**推荐算法**基于标签重叠/作者/类型相似度
- 线程安全：httplib 线程池并发处理请求，数据访问通过单一互斥锁串行化
- 访问日志：记录方法、路径、状态码与耗时

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
- 运行测试需要 `curl`

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
       "progress":0.5,"score":9,"comment":"震撼","tags":["科幻","中国文学"]}'

# 查列表：只看书，按评分倒序，第一页
curl 'http://127.0.0.1:8000/api/items?type=book&sort=-score&limit=10'

# 改一条记录（只传要改的字段）
curl -X PUT http://127.0.0.1:8000/api/items/1 \
  -H 'Content-Type: application/json' \
  -d '{"progress":1,"score":10}'

# 撤销刚才的修改 / 重做
curl -X POST http://127.0.0.1:8000/api/undo
curl -X POST http://127.0.0.1:8000/api/redo

# 和《三体》相似的条目
curl 'http://127.0.0.1:8000/api/recommend?item_id=1&topN=5'

# 删除
curl -i -X DELETE http://127.0.0.1:8000/api/items/1
```

### 接口测试

```bash
./scripts/run.sh --db /tmp/metrace-test.json &   # 测试会写数据，建议用临时文件
./tests/test_api.sh http://127.0.0.1:8000
```

脚本会用 `curl` 跑断言，覆盖增删改查、分页过滤与参数校验，最后打印通过 / 失败数量。

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
- ID 为 JSON 数字；时间格式 `YYYY-MM-DD HH:MM:SS`（**UTC**）
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
| 500 | 服务端异常 |

### 接口一览

| 方法 | 路径 | 说明 | 参数 / 请求体 | 响应 |
|---|---|---|---|---|
| GET | `/` | 纯文本存活探测 | — | `MeTrace-Server is running` |
| GET | `/ping` | 健康检查 | — | `{"status":"ok","service":"MeTrace-Server"}` |
| GET | `/api/items` | 条目列表 | `?type=&tag=&sort=&limit=&offset=` | `{"total":N,"items":[Item,...]}` |
| POST | `/api/items` | 新增条目 | Item 创建字段（`type`、`title` 必填） | `201` + `Item` |
| GET | `/api/items/{id}` | 单个条目 | — | `Item` |
| PUT | `/api/items/{id}` | 部分更新 | 仅 `progress`/`score`/`comment`/`tags` | `Item` |
| DELETE | `/api/items/{id}` | 删除条目 | — | `204`（无 body） |
| GET | `/api/tags` | 标签列表 | `?limit=&offset=` | `{"total":N,"items":[Tag,...]}` |
| POST | `/api/tags` | 新增标签 | `{"name":"科幻"}`，同名幂等 | `201` + `Tag` |
| POST | `/api/undo` | 撤销上一次写操作 | — | `{"success":true,"action":"..."}` |
| POST | `/api/redo` | 重做 | — | `{"success":true,"action":"..."}` |
| GET | `/api/recommend` | 相似推荐 | `?item_id=&topN=` | `{"items":[Item,...]}` |

> 计划中：`/api/search`（Trie 前缀搜索）、`/api/stats`（统计聚合），见 [开发进度](#开发进度)。

### 查询参数

| 参数 | 取值 | 说明 |
|---|---|---|
| `type` | `book` / `movie` / `music` | 缺省不过滤 |
| `tag` | 标签名称 | 按名称精确匹配 |
| `sort` | `created_at` / `score` / `date` / `title`，前缀 `-` 表示倒序 | 默认 `-created_at` |
| `limit` | 1~100 | 默认 20，超出范围会被截断 |
| `offset` | ≥ 0 | 默认 0 |
| `item_id` | 条目 id | `/api/recommend` 缺省时返回全局高分榜（冷启动兜底） |
| `topN` | 正整数，默认 10，上限 50 | 推荐条数 |

取值非法（例如 `type=game`、`sort=unknown`）一律返回 400，而不是静默忽略。

### 数据模型

**Item**（以 `include/core/Item.h` 为准）

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` | number | 主键，服务端生成，删除后不复用 |
| `type` | string | `book` / `movie` / `music` |
| `title` | string | 标题，必填，≤ 200 字符 |
| `author` | string | 作者 / 导演 / 艺术家，空串表示未知 |
| `description` | string | 简介，空串表示无 |
| `date` | string | 发布日期，推荐 `YYYY-MM-DD`，空串表示未知 |
| `progress` | number | 进度比例 `0`~`1`，默认 `0` |
| `score` | number | 评分 `1`~`10` 的整数，**`0` 表示未评分** |
| `comment` | string | 短评，空串表示无 |
| `tags` | string[] | 标签名称数组，按名称排序，单个条目最多 20 个 |
| `created_at` | string | 创建时间（UTC），只读 |
| `updated_at` | string | 更新时间（UTC），每次 PUT 自动刷新 |

条目字段分为两类：**用户可编辑**（`progress`、`score`、`comment`、`tags`，可通过 PUT 修改）与**只读**
（`type`、`title`、`author`、`description`、`date`，来源为本地存储或用户上传的条目信息，创建后不可通过接口修改）。

**Tag**

| 字段 | 类型 | 说明 |
|---|---|---|
| `id` | number | 主键 |
| `name` | string | 标签名，全局唯一，≤ 50 字符 |

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
  "score": 9,
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
  "score": 9,
  "comment": "震撼",
  "tags": ["中国文学", "科幻"],
  "created_at": "2026-09-29 10:00:00",
  "updated_at": "2026-09-29 10:00:00"
}
```

说明：

- `tags` 中不存在的标签会被自动创建并写入全局标签表；重复标签自动去重。
- 未提供的可选字段使用默认值（`progress`→`0`、`score`→`0`、字符串→空串、`tags`→`[]`）。
- 响应回传完整记录（含服务端生成的 `id` 与时间戳），便于客户端直接刷新本地缓存。

**PUT /api/items/{id}** 是部分更新，且只接受 4 个用户可编辑字段：

```bash
curl -X PUT http://127.0.0.1:8000/api/items/1 \
  -H 'Content-Type: application/json' \
  -d '{"score": 10}'
# 只改评分，其余字段不变
```

- 出现在请求体里的字段才会被写入，未出现的字段保持原值。
- `tags` 传数组即整体替换，传 `[]` 即清空标签。
- 请求体中出现只读字段或 `null` 一律返回 400——只读字段没有合法的修改途径，静默忽略会掩盖客户端 bug。

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
      "score": 10,
      "tags": ["中国文学", "科幻"],
      "created_at": "2026-09-29 10:00:00",
      "updated_at": "2026-09-29 10:30:00"
    }
  ]
}
```

`total` 是**满足过滤条件的总条数**（不受分页影响），用于客户端计算页数；`items` 只包含当前页。

**POST /api/undo / /api/redo**

```json
{ "success": true, "action": "新增《三体》" }
```

撤销/重做由内存双栈实现：只有写操作（增/改/删条目、新增标签）入栈，栈空时返回 400 `nothing to undo`；
撤销栈上限 100 条；**服务重启后撤销/重做历史清空**（数据本身不受影响）。

**GET /api/recommend?item_id=1&topN=5**

返回与指定条目最相似的 TopN 条目（不含自身）。相似度 = `3 × 共同标签数 + 2 × 同作者 + 1 × 同类型`，
同分时评分高者优先。`item_id` 缺省时返回全局高分榜。

---

## 数据存储

单个 JSON 文件（默认 `./data/metrace.json`）：

```json
{
  "next_id": 42,
  "items": [ { "id": 1, "type": "book", "title": "三体", "...": "..." } ],
  "tags":  [ { "id": 1, "name": "科幻" } ]
}
```

- **加载**：启动时全量读入内存，逐条反序列化建链表；文件不存在按空库启动。
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
HTTP 层 (src/http/Router.cpp)      路由注册 · 参数校验 · JSON 编解码
    ▼
业务层 (service/DataBase)          加载/保存 · CRUD 编排 · 撤销重做 · 推荐入口
    ▼
数据结构与算法层 (core/)           LinkedList · UndoStack · Recommender · Item/Tag
    ▼
本地 JSON 文件 (data/metrace.json)
```

分层职责，禁止跨层：HTTP 层不直接操作链表与文件；业务层不 include httplib；core 层不关心 HTTP 状态码。

```text
MeTrace-Server/
├── CMakeLists.txt
├── AGENT.md                     # 开发指南（架构约定、接口规格与阶段计划）
├── README.md                    # 本文件
├── include/
│   ├── core/                    # 手写数据结构与数据模型（Item / LinkedList / UndoStack / Recommender …）
│   ├── service/
│   │   └── DataBase.h           # 数据库门面：链表 + 标签表 + 加载保存 + undo/redo 编排
│   └── http/
│       └── Router.h
├── src/                         # 与 include/ 一一对应的实现 + main.cpp
├── scripts/
│   ├── build.sh
│   └── run.sh
├── tests/
│   └── test_api.sh
├── data/                        # 运行时生成的 JSON 存储文件（已 gitignore）
└── build/                       # 构建产物，含 FetchContent 缓存的依赖
```

### 线程安全

httplib 默认用线程池并发处理请求，而内存链表不是线程安全的。`DataBase` 持有一把 `std::mutex`，
所有公开方法（含撤销/重做与保存）进入即加锁，同一时刻只有一个线程操作数据——对这个量级的个人应用足够。

---

## 数据结构与算法

本项目为数据结构与算法课设，以下结构均为手写实现并承担真实业务（非摆设）：

| 结构 / 算法 | 关键操作与复杂度 | 用途 |
|---|---|---|
| 单链表 `LinkedList` | 头插 O(1)，按 id 查找/删除 O(n) | 全部条目的主存储，所有 CRUD 在链表上进行 |
| 双栈 `UndoStack` | push / undo / redo 均摊 O(1)，容量 100 | 撤销 / 重做，保存操作前后的条目快照 |
| 推荐算法 `Recommender` | 遍历打分 O(n·k)，或图共同邻居 O(V+E) | 相似推荐；高分榜用排序 O(n log n) |
| 前缀树 `Trie`（计划） | 插入 / 前缀查询 O(L) | 标题 / 作者前缀搜索 |
| 二叉堆 / 排序（计划） | push/pop O(log n) | 推荐与高分榜的 TopN |
| 哈希表（计划） | 平均 O(1) | id → 条目索引，加速单查/改/删 |

---

## 开发进度

### 阶段 0：清理残留，恢复编译（9.29~9.30）

- [ ] 移除 CMake 中残留的数据库依赖
- [ ] 补全 Item 序列化与访问器；重建最小路由（`/`、`/ping`）

### 阶段 1：数据层（10.1~10.3）

- [ ] `LinkedList` 实现 + 单元测试
- [ ] `DataBase` 加载 / 原子保存、`next_id`、标签表

### 阶段 2：HTTP CRUD（10.4~10.7）

- [ ] `/api/items`、`/api/tags` 全部路由 + 校验 + 过滤排序分页
- [ ] `tests/test_api.sh` 字段适配，断言全部通过

### 阶段 3：撤销重做与推荐（10.8~10.12）

- [ ] `UndoStack` + `/api/undo`、`/api/redo` + 写操作入栈
- [ ] `Recommender` + `/api/recommend`
- [ ] 可选：`Trie` + `/api/search`、二叉堆 / 哈希表接入

### 阶段 4：完善与验收（10.13~10.16）

- [ ] 50~100 条演示数据
- [ ] 性能测试（1000 条数据下的推荐响应时间）
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

表示"未评分"。有效评分为 1~10 的整数；响应中不使用 `null`。

**撤销/重做重启后还在吗？**

不在。撤销栈是进程内存结构，重启即清空；已写入 JSON 文件的数据不受影响。

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

---

## 相关文档

- [`AGENT.md`](AGENT.md) —— 开发指南：架构约定、接口规格、数据结构要求与阶段计划
