#!/usr/bin/env bash
#
# MeTrace-Server 接口回归测试（自包含）。
#
# 覆盖当前已实现的全部接口（见 README §接口一览 / AGENT.md §8.2）：
#   GET  /                    纯文本存活探测
#   GET  /ping                健康检查
#   GET  /api/items           过滤 / 排序 / 分页（Top-N 淘汰堆）
#   POST /api/items           新增（默认值、标签自动注册与去重）
#   GET/PUT/DELETE /api/items/{id}
#   GET  /api/tags            标签表有序枚举
# 同时覆盖 DataBase::load 的种子加载语义（幽灵标签丢弃、tags 排序去重）与
# 启动行为（文件不存在按空库启动；文件损坏或字段非法一律 fail-fast 拒绝启动、不改写文件），
# 以及 type 白名单（含 uncategorized）在查询 / 创建 / 更新 / 加载四个入口的一致性。
#
# 用法：
#   ./tests/test.sh                     # 用 tests/metrace.json 起一个临时服务端跑全部断言（推荐）
#   ./tests/test.sh --url URL           # 对已运行的服务端测试（跳过依赖种子数据的断言）
#   ./tests/test.sh --server PATH       # 指定 server 可执行文件（默认 build/bin/server）
#   ./tests/test.sh --port N            # 指定自启服务端端口（默认自动挑空闲端口）
#
# 依赖：bash、curl、jq。
# 说明：自启模式下所有写操作都落在临时目录里的数据库副本上，
#       不会改动 tests/metrace.json、data/ 或任何已有服务端。

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURE="$ROOT/tests/metrace.json"

SERVER_BIN="${METRACE_SERVER_BIN:-$ROOT/build/bin/server}"
BASE_URL=""
PORT=""
MANAGED=1     # 1 = 由本脚本启动服务端
SEED_MODE=1   # 1 = 断言 tests/metrace.json 的加载结果
CT='Content-Type: application/json'

usage() {
    cat <<'EOF'
用法: tests/test.sh [选项]

  (无选项)            用 tests/metrace.json 起一个临时服务端并测试全部接口
  --url URL           对已运行的服务端测试（跳过依赖种子数据的断言）
  --server PATH       指定 server 可执行文件（默认 build/bin/server）
  --port N            指定自启服务端端口（默认自动挑空闲端口）
  -h, --help          显示本帮助

依赖: bash、curl、jq。
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --url)
            BASE_URL="${2:?--url 需要一个地址}"; MANAGED=0; SEED_MODE=0; shift 2 ;;
        --url=*)
            BASE_URL="${1#*=}"; MANAGED=0; SEED_MODE=0; shift ;;
        --server)
            SERVER_BIN="${2:?--server 需要一个路径}"; shift 2 ;;
        --server=*)
            SERVER_BIN="${1#*=}"; shift ;;
        --port)
            PORT="${2:?--port 需要一个端口}"; shift 2 ;;
        --port=*)
            PORT="${1#*=}"; shift ;;
        -h|--help)
            usage; exit 0 ;;
        http://*|https://*)
            BASE_URL="$1"; MANAGED=0; SEED_MODE=0; shift ;;
        *)
            echo "未知参数: $1" >&2; usage; exit 2 ;;
    esac
done

for tool in curl jq; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "缺少依赖: $tool（需要 curl 与 jq）" >&2
        exit 2
    fi
done

PASS=0
FAIL=0

check() { # <描述> <期望> <实际>
    if [[ "$2" == "$3" ]]; then
        printf '  ok   %s\n' "$1"
        PASS=$((PASS + 1))
    else
        printf '  FAIL %s (期望 [%s] 实际 [%s])\n' "$1" "$2" "$3"
        FAIL=$((FAIL + 1))
    fi
}

jqv() { printf '%s' "$2" | jq -r "$1"; }   # 取标量（字符串不带引号）
jqc() { printf '%s' "$2" | jq -c "$1"; }   # 取结构（紧凑单行，便于比较）

checkjq()  { check "$1" "$2" "$(jqv "$3" "$4")"; }
checkjqc() { check "$1" "$2" "$(jqc "$3" "$4")"; }

# 发一次请求，把响应体与状态码分别放进 RESP_BODY / RESP_STATUS。
request() {
    local out
    out="$(curl -s -w $'\n%{http_code}' "$@")"
    RESP_STATUS="${out##*$'\n'}"
    RESP_BODY="${out%$'\n'*}"
}

api_get()    { request "$BASE$1"; }
api_post()   { request -X POST "$BASE/api/items" -H "$CT" -d "$1"; }
api_put()    { request -X PUT "$BASE/api/items/$1" -H "$CT" -d "$2"; }
api_delete() { request -X DELETE "$BASE/api/items/$1"; }

# 只需要状态码时用这几个，避免污染 RESP_*。
status_of()     { curl -s -o /dev/null -w '%{http_code}' "$@"; }
status_get()    { status_of "$BASE$1"; }
status_post()   { status_of -X POST "$BASE/api/items" -H "$CT" -d "$1"; }
status_put()    { status_of -X PUT "$BASE/api/items/$1" -H "$CT" -d "$2"; }
status_delete() { status_of -X DELETE "$BASE/api/items/$1"; }

content_type() { # <path>
    curl -s -D - -o /dev/null "$BASE$1" | tr -d '\r' \
        | awk -F': ' 'tolower($1) == "content-type" { print $2; exit }'
}

free_port() { # <起始端口> <结束端口>
    local p
    for ((p = $1; p <= $2; ++p)); do
        if ! (exec 3<>"/dev/tcp/127.0.0.1/$p") 2>/dev/null; then
            printf '%s\n' "$p"
            return 0
        fi
    done
    return 1
}

SRV_PIDS=()
WORK=""
SERVER_PID=""

start_server() { # <db> <port> <log>，成功返回 0
    local db="$1" port="$2" log="$3" i
    "$SERVER_BIN" --host 127.0.0.1 --port "$port" --db "$db" >"$log" 2>&1 &
    SERVER_PID=$!
    SRV_PIDS+=("$SERVER_PID")
    for i in $(seq 1 50); do
        if curl -sf "http://127.0.0.1:$port/ping" >/dev/null 2>&1; then
            return 0
        fi
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            return 1
        fi
        sleep 0.1
    done
    return 1
}

# 以一条合法条目为底，用 jq 补丁改字段后写成单条目数据库（供加载校验用例使用）。
DB_ITEM_BASE='{"id":1,"type":"book","title":"基准条目","author":"","description":"","date":"","progress":0,"score":0,"comment":"","tags":[],"created_at":1000,"updated_at":1000}'

make_db_file() { # <path> <jq-patch>
    local item
    item="$(printf '%s' "$DB_ITEM_BASE" | jq -c "$2")"
    jq -cn --argjson item "$item" '{next_id:($item.id + 1), tags:[], items:[$item]}' >"$1"
}

# 期望服务端因加载校验失败拒绝启动：非零退出 + 日志含 Fatal + 数据文件未被改写。
expect_load_failure() { # <描述> <db-path>
    local desc="$1" db="$2" port log pid rc i before after
    before="$(cat "$db")"
    port="$(free_port 19400 19500)"
    log="$WORK/loadfail.$RANDOM.log"
    "$SERVER_BIN" --host 127.0.0.1 --port "$port" --db "$db" >"$log" 2>&1 &
    pid=$!
    SRV_PIDS+=("$pid")
    for i in $(seq 1 60); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.05
    done
    if kill -0 "$pid" 2>/dev/null; then
        check "$desc" '拒绝启动' '仍在运行（未触发 fail-fast）'
        kill "$pid" 2>/dev/null
        return
    fi
    wait "$pid" 2>/dev/null
    rc=$?
    check "$desc（非零退出）" true "$([[ "$rc" -ne 0 ]] && echo true || echo false)"
    check "$desc（日志含 Fatal）" true "$(grep -qi 'fatal' "$log" && echo true || echo false)"
    after="$(cat "$db")"
    check "$desc（未改写数据文件）" "$before" "$after"
}

cleanup() {
    local pid
    for pid in "${SRV_PIDS[@]:-}"; do
        [[ -n "$pid" ]] && kill "$pid" 2>/dev/null
    done
    wait 2>/dev/null
    [[ -n "$WORK" ]] && rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------------------
# 启动：自启模式在临时目录里跑 tests/metrace.json 的副本
# ---------------------------------------------------------------------------
WORK="$(mktemp -d)"

if [[ "$MANAGED" -eq 1 ]]; then
    if [[ ! -x "$SERVER_BIN" ]]; then
        echo "找不到可执行文件: $SERVER_BIN（先跑 ./scripts/build.sh，或用 --server 指定）" >&2
        exit 2
    fi
    if [[ ! -f "$FIXTURE" ]]; then
        echo "找不到测试数据: $FIXTURE" >&2
        exit 2
    fi

    if [[ -z "$PORT" ]]; then
        PORT="$(free_port 18100 18200)" || {
            echo "找不到空闲端口（18100~18200）" >&2; exit 1; }
    fi
    BASE="http://127.0.0.1:$PORT"
    cp "$FIXTURE" "$WORK/db.json"

    echo "== 启动测试服务端 (port $PORT, db $WORK/db.json) =="
    if ! start_server "$WORK/db.json" "$PORT" "$WORK/server.log"; then
        echo "服务端启动失败，日志：" >&2
        cat "$WORK/server.log" >&2
        exit 1
    fi
else
    BASE="${BASE_URL%/}"
    echo "== 对已运行的服务端测试: $BASE（跳过种子数据断言）=="
fi

# ---------------------------------------------------------------------------
echo
echo "== 健康检查与路由 =="
# ---------------------------------------------------------------------------
api_get '/ping'
check 'GET /ping 返回 200' 200 "$RESP_STATUS"
checkjq 'GET /ping status=ok' 'ok' '.status' "$RESP_BODY"
checkjq 'GET /ping service 名正确' 'MeTrace-Server' '.service' "$RESP_BODY"
check 'GET /ping Content-Type 为 JSON' 'application/json; charset=utf-8' "$(content_type '/ping')"

api_get '/'
check 'GET / 返回 200' 200 "$RESP_STATUS"
check 'GET / 返回存活文案' 'MeTrace-Server is running' "$RESP_BODY"
check 'GET / Content-Type 为文本' 'text/plain; charset=utf-8' "$(content_type '/')"

api_get '/api/nope'
check '未知路由 /api/nope 返回 404' 404 "$RESP_STATUS"
api_get '/api/items/abc'
check '非数字 id /api/items/abc 返回 404' 404 "$RESP_STATUS"
api_get '/api/items/999999'
check '不存在的 id 返回 404' 404 "$RESP_STATUS"
checkjq '404 响应体是 {"error":...}' true '(.error | type) == "string" and (.error | length) > 0' "$RESP_BODY"

# ---------------------------------------------------------------------------
if [[ "$SEED_MODE" -eq 1 ]]; then
echo
echo "== 种子数据加载 (tests/metrace.json) =="
# ---------------------------------------------------------------------------
api_get '/api/items?limit=100'
SEED_ALL="$RESP_BODY"
check 'GET /api/items 返回 200' 200 "$RESP_STATUS"
checkjq '种子条目总数 21' 21 '.total' "$SEED_ALL"
checkjq '本页条目数 21' 21 '.items | length' "$SEED_ALL"

api_get '/api/items/1'
check 'GET /api/items/1 返回 200' 200 "$RESP_STATUS"
checkjq 'id=1 title' '三体' '.title' "$RESP_BODY"
checkjq 'id=1 type' 'book' '.type' "$RESP_BODY"
checkjq 'id=1 author' '刘慈欣' '.author' "$RESP_BODY"
checkjq 'id=1 date' '2008-01-01' '.date' "$RESP_BODY"
checkjq 'id=1 score' 95 '.score' "$RESP_BODY"
checkjq 'id=1 progress' true '.progress == 1.0' "$RESP_BODY"
checkjq 'id=1 created_at' 1789899300 '.created_at' "$RESP_BODY"
checkjqc 'id=1 tags 有序' '["中国文学","小说","科幻","经典"]' '.tags' "$RESP_BODY"
check '响应中不出现 null' 0 "$(printf '%s' "$RESP_BODY" | grep -c ':null')"

# item 7 在种子文件里带了一个未注册的 test_tag（幽灵引用）且顺序未归一，
# DataBase::load 应丢弃幽灵标签并由 setTags 排序去重（AGENT.md §7.3①）。
api_get '/api/items/7'
checkjqc '幽灵标签被丢弃且 tags 排序去重' '["外国文学","小说","悬疑"]' '.tags' "$RESP_BODY"

checkjq '每个条目的 tags 都有序去重' true \
    '[.items[].tags | . == (sort | unique)] | all' "$SEED_ALL"

api_get '/api/items?type=book&limit=100'
checkjq 'type=book 总数 8' 8 '.total' "$RESP_BODY"
checkjq 'type=book 结果类型全对' true '[.items[].type] | all(. == "book")' "$RESP_BODY"
api_get '/api/items?type=movie&limit=100'
checkjq 'type=movie 总数 7' 7 '.total' "$RESP_BODY"
api_get '/api/items?type=music&limit=100'
checkjq 'type=music 总数 5' 5 '.total' "$RESP_BODY"
api_get '/api/items?type=book&tag=科幻&limit=100'
checkjq 'type+tag 组合过滤总数 3' 3 '.total' "$RESP_BODY"
checkjq '组合过滤结果都含该标签' true '[.items[].tags | index("科幻") != null] | all' "$RESP_BODY"
api_get '/api/items?tag=不存在的标签'
checkjq '不存在的标签返回空集' 0 '.total' "$RESP_BODY"
checkjqc '不存在的标签 items 为空数组' '[]' '.items' "$RESP_BODY"

# 未分类条目（type = uncategorized）：改名后的新取值必须能正常加载与过滤
api_get '/api/items?type=uncategorized&limit=100'
check 'type=uncategorized 查询返回 200' 200 "$RESP_STATUS"
checkjq 'type=uncategorized 总数 1' 1 '.total' "$RESP_BODY"
checkjqc '未分类条目的 id 与 type' '{"id":21,"type":"uncategorized"}' '.items[0] | {id, type}' "$RESP_BODY"
api_get '/api/items?type=uncategorized&tag=小说'
checkjq '未分类条目可被标签交叉过滤' 1 '.total' "$RESP_BODY"

# ---------------------------------------------------------------------------
echo
echo "== 排序与分页 =="
# ---------------------------------------------------------------------------
api_get '/api/items?sort=created_at&limit=3'
checkjqc 'sort=created_at 升序' '[1,2,3]' '[.items[].id]' "$RESP_BODY"
api_get '/api/items?sort=-created_at&limit=3'
checkjqc 'sort=-created_at 降序' '[20,19,18]' '[.items[].id]' "$RESP_BODY"
api_get '/api/items?limit=3'
checkjqc '默认排序 = -created_at' '[20,19,18]' '[.items[].id]' "$RESP_BODY"

api_get '/api/items?sort=-score&limit=3'
checkjqc 'sort=-score 高分在前' '[2,1,12]' '[.items[].id]' "$RESP_BODY"
api_get '/api/items?sort=score&limit=3'
checkjqc 'sort=score 低分在前' '[19,9,18]' '[.items[].id]' "$RESP_BODY"
api_get '/api/items?sort=score&limit=100'
checkjqc '未评分(score=0)无论方向都排最后' '[10,14,20,21]' '[.items[] | select(.score == 0) | .id]' "$RESP_BODY"
check '未评分条目确实位于结果末尾' true "$(jqv '(.items[-4:] | all(.score == 0))' "$RESP_BODY")"

api_get '/api/items?sort=title&limit=1'
checkjq 'sort=title 按 UTF-8 字节序升序' 'Fly Me to the Moon' '.items[0].title' "$RESP_BODY"
api_get '/api/items?sort=-title&limit=1'
checkjq 'sort=-title 降序' '黑暗森林' '.items[0].title' "$RESP_BODY"

api_get '/api/items?sort=date&limit=1'
checkjqc 'sort=date 空日期排最前' '{"id":15,"date":""}' '.items[0] | {id, date}' "$RESP_BODY"
api_get '/api/items?sort=-date&limit=1'
checkjq 'sort=-date 最新日期在前' '2019-05-30' '.items[0].date' "$RESP_BODY"
api_get '/api/items?sort=-date&limit=100'
checkjq 'sort=-date 空日期排最后' 15 '.items[-1].id' "$RESP_BODY"

# 分页不重不漏：把 limit=7 的每一页拼起来，应与 limit=100 的全量一致
api_get '/api/items?sort=-score&limit=100'
FULL_IDS="$(jqc '[.items[].id]' "$RESP_BODY")"
PAGED_IDS='[]'
OFFSET=0
while [[ "$OFFSET" -lt 21 ]]; do
    api_get "/api/items?sort=-score&limit=7&offset=$OFFSET"
    checkjq "分页 offset=$OFFSET 时 total 不受分页影响" 21 '.total' "$RESP_BODY"
    check "分页 offset=$OFFSET 每页不超过 limit" true \
        "$(jqv '(.items | length) <= 7' "$RESP_BODY")"
    PAGED_IDS="$(printf '%s' "$RESP_BODY" | jq -c --argjson prev "$PAGED_IDS" '$prev + [.items[].id]')"
    OFFSET=$((OFFSET + 7))
done
check '分页逐页拼接与全量一致（id 全序兜底）' "$FULL_IDS" "$PAGED_IDS"

api_get '/api/items?limit=0'
check 'limit=0 静默截断为 1' true "$(jqv '(.items | length) == 1 and .total == 21' "$RESP_BODY")"
api_get '/api/items?limit=101'
check 'limit 超过 100 静默截断' true "$(jqv '(.items | length) == 21 and .total == 21' "$RESP_BODY")"
api_get '/api/items?offset=1000'
check 'offset 超出总数返回空页但 total 不变' true "$(jqv '(.items | length) == 0 and .total == 21' "$RESP_BODY")"

echo "== 查询参数校验 =="
for q in 'type=game' 'type=null' 'sort=unknown' 'limit=abc' 'offset=-1' 'offset=abc'; do
    check "GET /api/items?$q 返回 400" 400 "$(status_get "/api/items?$q")"
done
check '空参数视同未提供（200）' 200 "$(status_get '/api/items?type=')"
check '未知参数被忽略（200）' 200 "$(status_get '/api/items?unknown_param=1')"

# ---------------------------------------------------------------------------
echo
echo "== 标签接口（种子） =="
# ---------------------------------------------------------------------------
SEED_TAGS='["中国文学","动画","原声","外国文学","小说","悬疑","摇滚","民谣","治愈","爵士","科幻","纪录片","经典","高分"]'
api_get '/api/tags?limit=100'
check 'GET /api/tags 返回 200' 200 "$RESP_STATUS"
checkjq '标签总数 14' 14 '.total' "$RESP_BODY"
checkjqc '标签按 UTF-8 字节序升序' "$SEED_TAGS" '.tags' "$RESP_BODY"
checkjq '幽灵标签 test_tag 未进表' true '(.tags | index("test_tag")) == null' "$RESP_BODY"

api_get '/api/tags?limit=3&offset=1'
checkjqc '标签分页切片正确' '["动画","原声","外国文学"]' '.tags' "$RESP_BODY"
checkjq '标签分页 total 不受影响' 14 '.total' "$RESP_BODY"
api_get '/api/tags?offset=1000'
check '标签 offset 超出返回空页' true "$(jqv '(.tags | length) == 0 and .total == 14' "$RESP_BODY")"
check '标签 limit 非数字返回 400' 400 "$(status_get '/api/tags?limit=abc')"
check '标签 offset 为负返回 400' 400 "$(status_get '/api/tags?offset=-1')"
else
echo
echo "== 跳过种子数据 / 排序分页 / 标签种子断言（--url 模式）=="
fi

# ---------------------------------------------------------------------------
echo
echo "== 条目 CRUD =="
# ---------------------------------------------------------------------------
api_post '{"type":"book","title":"测试条目","author":"测试作者","description":"测试简介","date":"2024-01-01","progress":0.5,"score":90,"comment":"测试短评","tags":["科幻","新标签","科幻"]}'
check 'POST /api/items 返回 201' 201 "$RESP_STATUS"
CREATE_RESP="$RESP_BODY"
NEW_ID="$(jqv '.id' "$CREATE_RESP")"
if [[ "$SEED_MODE" -eq 1 ]]; then
    checkjq '服务端分配 id（种子 next_id=22）' 22 '.id' "$CREATE_RESP"
fi
checkjq 'created_at 与 updated_at 一致' true '.created_at == .updated_at and .created_at > 0' "$CREATE_RESP"
checkjqc '重复标签去重且有序' '["新标签","科幻"]' '.tags' "$CREATE_RESP"
checkjq 'progress 保留小数' true '.progress == 0.5' "$CREATE_RESP"

api_get "/api/items/$NEW_ID"
check 'GET /api/items/{id} 回读成功' 200 "$RESP_STATUS"
check '回读内容与创建响应一致' "$CREATE_RESP" "$RESP_BODY"

api_post '{"type":"music","title":"默认值条目"}'
checkjq '缺省字段用默认值' true \
    '.author == "" and .description == "" and .date == "" and .comment == "" and .score == 0 and .progress == 0 and .tags == []' "$RESP_BODY"

api_post '{"type":"uncategorized","title":"未分类新增条目"}'
check 'POST type=uncategorized 返回 201' 201 "$RESP_STATUS"
checkjq 'POST 的 uncategorized 原样回传' 'uncategorized' '.type' "$RESP_BODY"

# PUT 是部分更新：只改传入字段
sleep 1
api_put "$NEW_ID" '{"score":98,"title":"测试条目(改)"}'
check 'PUT /api/items/{id} 返回 200' 200 "$RESP_STATUS"
checkjq 'PUT 更新传入字段' true '.score == 98 and .title == "测试条目(改)"' "$RESP_BODY"
checkjq 'PUT 未传字段保持原值' true '.author == "测试作者" and .description == "测试简介"' "$RESP_BODY"
checkjq 'PUT 刷新 updated_at' true '.updated_at > .created_at' "$RESP_BODY"
api_put "$NEW_ID" '{"progress":0.25}'
checkjq 'PUT 可改浮点 progress' true '.progress == 0.25' "$RESP_BODY"

api_put "$NEW_ID" '{"tags":[]}'
checkjqc 'PUT tags=[] 清空标签' '[]' '.tags' "$RESP_BODY"
api_put "$NEW_ID" '{"tags":["科幻"]}'
checkjqc 'PUT tags 整体替换' '["科幻"]' '.tags' "$RESP_BODY"
api_put "$NEW_ID" '{"type":"movie"}'
checkjq 'PUT 可改 type' 'movie' '.type' "$RESP_BODY"
api_put "$NEW_ID" '{"type":"uncategorized"}'
checkjq 'PUT 可改为 uncategorized' 'uncategorized' '.type' "$RESP_BODY"
api_put "$NEW_ID" '{}'
check 'PUT 空对象合法（不发变更）' 200 "$RESP_STATUS"
checkjq 'PUT 空对象不改字段' 98 '.score' "$RESP_BODY"

api_get '/api/tags?limit=100'
checkjq '新建条目的新标签自动注册' true '(.tags | index("新标签")) != null' "$RESP_BODY"

api_delete "$NEW_ID"
check 'DELETE /api/items/{id} 返回 204' 204 "$RESP_STATUS"
check 'DELETE 响应无 body' '' "$RESP_BODY"
api_get "/api/items/$NEW_ID"
check '删除后 GET 返回 404' 404 "$RESP_STATUS"
api_delete "$NEW_ID"
check '重复 DELETE 返回 404' 404 "$RESP_STATUS"

# 删除条目不级联删标签（孤儿标签是可接受的简化）
api_post '{"type":"book","title":"孤儿标签条目","tags":["临时标签"]}'
ORPHAN_ID="$(jqv '.id' "$RESP_BODY")"
api_delete "$ORPHAN_ID"
api_get '/api/tags?limit=100'
checkjq '删除条目不级联删除标签' true '(.tags | index("临时标签")) != null' "$RESP_BODY"

# id 不复用：删除后 next_id 继续前进
api_post '{"type":"book","title":"id 不复用 A"}'
ID_A="$(jqv '.id' "$RESP_BODY")"
api_delete "$ID_A"
api_post '{"type":"book","title":"id 不复用 B"}'
ID_B="$(jqv '.id' "$RESP_BODY")"
check '删除后的 id 不复用' "$((ID_A + 1))" "$ID_B"

# ---------------------------------------------------------------------------
echo
echo "== 请求体校验 =="
# ---------------------------------------------------------------------------
api_post '{"type":"book","title":"校验靶子"}'
VAL_ID="$(jqv '.id' "$RESP_BODY")"
VAL_BEFORE="$RESP_BODY"
TAGS21="$(jq -cn '[range(0;21) | "tag\(.)"] | {type:"book", title:"超量标签", tags:.}')"
TAGS20="$(jq -cn '[range(0;20) | "tag\(.)"] | {type:"book", title:"满量标签", tags:.}')"
LONG200="$(printf 'a%.0s' $(seq 1 200))"
LONG201="$(printf 'a%.0s' $(seq 1 201))"
TAG50="$(printf 'a%.0s' $(seq 1 50))"
TAG51="$(printf 'a%.0s' $(seq 1 51))"

check 'POST 缺 title 返回 400' 400 "$(status_post '{"type":"book"}')"
check 'POST 缺 type 返回 400' 400 "$(status_post '{"title":"x"}')"
check 'POST 非法 type 返回 400' 400 "$(status_post '{"type":"game","title":"x"}')"
check 'POST type=null（旧枚举名）返回 400' 400 "$(status_post '{"type":"null","title":"x"}')"
check 'POST 未知字段返回 400' 400 "$(status_post '{"type":"book","title":"x","foo":1}')"
check 'POST 只读字段 id 返回 400' 400 "$(status_post '{"type":"book","title":"x","id":1}')"
check 'POST 只读字段 created_at 返回 400' 400 "$(status_post '{"type":"book","title":"x","created_at":1}')"
check 'POST title 空串返回 400' 400 "$(status_post '{"type":"book","title":""}')"
check 'POST title 超过 200 返回 400' 400 "$(status_post "{\"type\":\"book\",\"title\":\"$LONG201\"}")"
check 'POST title 恰好 200 合法' 201 "$(status_post "{\"type\":\"book\",\"title\":\"$LONG200\"}")"
check 'POST score 非整数返回 400' 400 "$(status_post '{"type":"book","title":"x","score":9.5}')"
check 'POST score 越界返回 400' 400 "$(status_post '{"type":"book","title":"x","score":101}')"
check 'POST score 为 null 返回 400' 400 "$(status_post '{"type":"book","title":"x","score":null}')"
check 'POST progress 越界返回 400' 400 "$(status_post '{"type":"book","title":"x","progress":2}')"
check 'POST progress 为负返回 400' 400 "$(status_post '{"type":"book","title":"x","progress":-0.1}')"
check 'POST tags 超过 20 个返回 400' 400 "$(status_post "$TAGS21")"
check 'POST tags 恰好 20 个合法' 201 "$(status_post "$TAGS20")"
check 'POST tags 元素非字符串返回 400' 400 "$(status_post '{"type":"book","title":"x","tags":[1]}')"
check 'POST tags 含空串返回 400' 400 "$(status_post '{"type":"book","title":"x","tags":[""]}')"
check 'POST tag 超过 50 字节返回 400' 400 "$(status_post "{\"type\":\"book\",\"title\":\"x\",\"tags\":[\"$TAG51\"]}")"
check 'POST tag 恰好 50 字节合法' 201 "$(status_post "{\"type\":\"book\",\"title\":\"x\",\"tags\":[\"$TAG50\"]}")"
check 'POST tags 非数组返回 400' 400 "$(status_post '{"type":"book","title":"x","tags":"科幻"}')"
check 'POST 空 body 返回 400' 400 "$(status_post '')"
check 'POST body 非 JSON 返回 400' 400 "$(status_post 'not json')"
check 'POST body 为数组返回 400' 400 "$(status_post '[]')"

check 'PUT 未知字段返回 400' 400 "$(status_put "$VAL_ID" '{"foo":1}')"
check 'PUT 只读字段返回 400' 400 "$(status_put "$VAL_ID" '{"id":1}')"
check 'PUT type 非法返回 400' 400 "$(status_put "$VAL_ID" '{"type":"game"}')"
check 'PUT title 空串返回 400' 400 "$(status_put "$VAL_ID" '{"title":""}')"
check 'PUT score 为 null 返回 400' 400 "$(status_put "$VAL_ID" '{"score":null}')"
check 'PUT progress 越界返回 400' 400 "$(status_put "$VAL_ID" '{"progress":2}')"
check 'PUT type=null（旧枚举名）返回 400' 400 "$(status_put "$VAL_ID" '{"type":"null"}')"
check 'PUT tags 含空串返回 400' 400 "$(status_put "$VAL_ID" '{"tags":[""]}')"
check 'PUT tag 超过 50 字节返回 400' 400 "$(status_put "$VAL_ID" "{\"tags\":[\"$TAG51\"]}")"
check 'PUT body 非 JSON 返回 400' 400 "$(status_put "$VAL_ID" 'oops')"
check 'PUT 不存在的 id 返回 404' 404 "$(status_put 999999 '{"score":1}')"
check 'DELETE 不存在的 id 返回 404' 404 "$(status_delete 999999)"

api_get "/api/items/$VAL_ID"
check '校验失败不改动已有数据' "$VAL_BEFORE" "$RESP_BODY"

# ---------------------------------------------------------------------------
if [[ "$MANAGED" -eq 1 ]]; then
echo
echo "== 启动与加载行为 =="
# ---------------------------------------------------------------------------
# 文件不存在：按空库启动
EMPTY_PORT="$(free_port 18200 18300)"
EMPTY_BASE="http://127.0.0.1:$EMPTY_PORT"
if start_server "$WORK/empty.json" "$EMPTY_PORT" "$WORK/empty.log"; then
    EMPTY_ITEMS="$(curl -s "$EMPTY_BASE/api/items")"
    check '数据库文件不存在时按空库启动' true \
        "$(jqv '(.total == 0) and (.items | length == 0)' "$EMPTY_ITEMS")"
    EMPTY_POST="$(curl -s -X POST "$EMPTY_BASE/api/items" -H "$CT" -d '{"type":"book","title":"空库首条"}')"
    checkjq '空库首个条目 id=1' 1 '.id' "$EMPTY_POST"
    kill "$SERVER_PID" 2>/dev/null
else
    check '数据库文件不存在时按空库启动' true false
    cat "$WORK/empty.log" >&2
fi

# ---------------------------------------------------------------------------
# 加载校验失败矩阵：值域 / 旧枚举名 / id 与时间戳，一律 fail-fast、不做迁移（AGENT §6.2/§16.9）
# ---------------------------------------------------------------------------
make_db_file "$WORK/bad_type_legacy.json" '.type = "null"'
expect_load_failure '加载拒绝 type="null"（旧枚举名，不迁移）' "$WORK/bad_type_legacy.json"
make_db_file "$WORK/bad_type_unknown.json" '.type = "game"'
expect_load_failure '加载拒绝未知 type' "$WORK/bad_type_unknown.json"
make_db_file "$WORK/bad_title_empty.json" '.title = ""'
expect_load_failure '加载拒绝空 title' "$WORK/bad_title_empty.json"
make_db_file "$WORK/bad_title_long.json" '.title = ("a" * 201)'
expect_load_failure '加载拒绝 201 字节 title' "$WORK/bad_title_long.json"
make_db_file "$WORK/bad_score.json" '.score = 101'
expect_load_failure '加载拒绝越界 score' "$WORK/bad_score.json"
make_db_file "$WORK/bad_progress.json" '.progress = 1.5'
expect_load_failure '加载拒绝越界 progress' "$WORK/bad_progress.json"
make_db_file "$WORK/bad_tag_empty.json" '.tags = [""]'
expect_load_failure '加载拒绝空 tag' "$WORK/bad_tag_empty.json"
make_db_file "$WORK/bad_tag_long.json" '.tags = ["a" * 51]'
expect_load_failure '加载拒绝 51 字节 tag' "$WORK/bad_tag_long.json"
make_db_file "$WORK/bad_tags_count.json" '.tags = [range(0;21) | "t\(.)"]'
expect_load_failure '加载拒绝 21 个 tags' "$WORK/bad_tags_count.json"
make_db_file "$WORK/bad_id_zero.json" '.id = 0'
expect_load_failure '加载拒绝 id=0' "$WORK/bad_id_zero.json"
make_db_file "$WORK/bad_timestamp.json" '.created_at = 2000'
expect_load_failure '加载拒绝 created_at > updated_at' "$WORK/bad_timestamp.json"

# 重复 id：单条目造不出来，手写两条同 id 记录
DUP_A="$(printf '%s' "$DB_ITEM_BASE" | jq -c '.')"
DUP_B="$(printf '%s' "$DB_ITEM_BASE" | jq -c '.title = "第二条"')"
jq -cn --argjson a "$DUP_A" --argjson b "$DUP_B" '{next_id:3, tags:[], items:[$a,$b]}' >"$WORK/bad_dup_id.json"
expect_load_failure '加载拒绝重复 id' "$WORK/bad_dup_id.json"

# 结构错误（缺字段）与 JSON 损坏同样 fail-fast
printf '{"next_id":2,"tags":[],"items":[{"id":1,"type":"book"}]}\n' >"$WORK/bad_missing_field.json"
expect_load_failure '加载拒绝缺字段条目' "$WORK/bad_missing_field.json"
printf '{ this is not valid json\n' >"$WORK/corrupt.json"
expect_load_failure '加载拒绝损坏的 JSON' "$WORK/corrupt.json"

# ---------------------------------------------------------------------------
# 正对照：uncategorized 与合法边界值必须能正常加载（防止校验过严）
# ---------------------------------------------------------------------------
make_db_file "$WORK/ok_uncategorized.json" '.type = "uncategorized"'
OK_PORT="$(free_port 19600 19700)"
OK_BASE="http://127.0.0.1:$OK_PORT"
if start_server "$WORK/ok_uncategorized.json" "$OK_PORT" "$WORK/ok_uncategorized.log"; then
    OK_ITEMS="$(curl -s "$OK_BASE/api/items")"
    checkjq '合法 uncategorized 库可加载' 1 '.total' "$OK_ITEMS"
    checkjq '加载后 type 保持 uncategorized' 'uncategorized' '.items[0].type' "$OK_ITEMS"
    kill "$SERVER_PID" 2>/dev/null
else
    check '合法 uncategorized 库可加载' true false
    cat "$WORK/ok_uncategorized.log" >&2
fi

make_db_file "$WORK/ok_boundary.json" \
    '.title = ("a" * 200) | .tags = [range(0;20) | "t\(.)" + ("a" * 40)] | .score = 100 | .progress = 1'
OK2_PORT="$(free_port 19700 19800)"
OK2_BASE="http://127.0.0.1:$OK2_PORT"
if start_server "$WORK/ok_boundary.json" "$OK2_PORT" "$WORK/ok_boundary.log"; then
    checkjq '合法边界值（200 字节 title / 20 个 tag / score 100 / progress 1）可加载' 1 \
        '.total' "$(curl -s "$OK2_BASE/api/items")"
    kill "$SERVER_PID" 2>/dev/null
else
    check '合法边界值（200 字节 title / 20 个 tag / score 100 / progress 1）可加载' true false
    cat "$WORK/ok_boundary.log" >&2
fi
fi

# ---------------------------------------------------------------------------
echo
echo "通过 $PASS 项，失败 $FAIL 项"
[[ "$FAIL" -eq 0 ]]
