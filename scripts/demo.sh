#!/bin/bash
# MeTrace-Server 演示脚本：自包含起停服务端，按顺序演示全部接口，跑完自动清理临时数据。
#
# 用法：
#   ./scripts/demo.sh                               # 用 scripts/seed_data.json 起临时服务端，自动挑空闲端口
#   ./scripts/demo.sh --port 9000                   # 指定端口
#   ./scripts/demo.sh --url http://127.0.0.1:8000   # 对已运行的服务端演示（会在其上创建/删除演示条目）
#   ./scripts/demo.sh --keep                        # 保留临时目录（演示数据），默认退出即删
#
# 依赖：curl 与 jq；自启模式需先构建（./scripts/build.sh）。
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_BIN="$ROOT/build/bin/server"
SEED="$ROOT/scripts/seed_data.json"

MANAGED=1
BASE=""
PORT=""
KEEP=0
WORK=""
SERVER_PID=""

usage() {
    cat <<'EOF'
用法: scripts/demo.sh [选项]

  (无选项)            用 scripts/seed_data.json 起一个临时服务端并演示全部接口
  --port N            指定自启服务端端口（默认自动挑空闲端口）
  --url URL           对已运行的服务端演示（跳过启动，会在其上创建/删除演示条目）
  --keep              保留临时目录与演示数据（默认退出时删除）
  -h, --help          显示本帮助

依赖: curl、jq。
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --url)    BASE="${2:?--url 需要一个地址}"; MANAGED=0; shift 2 ;;
        --url=*)  BASE="${1#*=}"; MANAGED=0; shift ;;
        --port)   PORT="${2:?--port 需要一个端口}"; shift 2 ;;
        --port=*) PORT="${1#*=}"; shift ;;
        --keep)   KEEP=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "未知参数: $1" >&2; usage; exit 2 ;;
    esac
done

for tool in curl jq; do
    command -v "$tool" >/dev/null 2>&1 || { echo "缺少依赖: $tool（需要 curl 与 jq）" >&2; exit 2; }
done

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

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    if [[ "$KEEP" -eq 0 && -n "$WORK" ]]; then
        rm -rf "$WORK"
    fi
}
trap cleanup EXIT INT TERM

if [[ "$MANAGED" -eq 1 ]]; then
    [[ -x "$SERVER_BIN" ]] || { echo "找不到可执行文件: $SERVER_BIN（先跑 ./scripts/build.sh）" >&2; exit 2; }
    [[ -f "$SEED" ]] || { echo "找不到演示数据: $SEED" >&2; exit 2; }

    if [[ -z "$PORT" ]]; then
        PORT="$(free_port 18080 18160)" || { echo "找不到空闲端口（18080~18160）" >&2; exit 1; }
    fi
    BASE="http://127.0.0.1:$PORT"
    WORK="$(mktemp -d)"
    cp "$SEED" "$WORK/db.json"
    "$SERVER_BIN" --host 127.0.0.1 --port "$PORT" --db "$WORK/db.json" >"$WORK/server.log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do
        curl -sf -m 1 "$BASE/ping" >/dev/null 2>&1 && break
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "服务端启动失败：" >&2
            cat "$WORK/server.log" >&2
            exit 1
        fi
        sleep 0.1
    done
    echo "MeTrace-Server 演示实例: $BASE"
    echo "演示数据: scripts/seed_data.json（40 条：书 / 影 / 音 + 23 个标签）"
else
    echo "MeTrace-Server 演示（对已运行的服务端）: $BASE"
fi

step() { echo; echo "== $* =="; }
req()  { curl -s -m 5 "$@"; }

# ---------------------------------------------------------------------------
step "1. 健康检查（GET /ping）"
req "$BASE/ping" | jq .

# ---------------------------------------------------------------------------
step "2. 列表：按评分倒序取前 5（GET /api/items?sort=-score&limit=5）"
req "$BASE/api/items?sort=-score&limit=5" | jq '{total, items: [.items[] | {id, title, score}]}'

step "3. 组合过滤：科幻电影（GET /api/items?type=movie&tag=科幻）"
req "$BASE/api/items?type=movie&tag=科幻" | jq '{total, titles: [.items[].title]}'

# ---------------------------------------------------------------------------
TARGET_ID="$(req "$BASE/api/items?limit=1&sort=created_at" | jq -r '.items[0].id')"
TARGET_TITLE="$(req "$BASE/api/items?limit=1&sort=created_at" | jq -r '.items[0].title')"

step "4. 单条读取（GET /api/items/$TARGET_ID，目标《$TARGET_TITLE》）"
req "$BASE/api/items/$TARGET_ID" | jq '{id, title, author, type, progress, score, tags}'

# ---------------------------------------------------------------------------
step "5. 标签：列表 + 新增（GET/POST /api/tags）"
req "$BASE/api/tags?limit=8" | jq '{total, tags}'
echo "-- POST {\"name\": \"演示标签\"} --"
req -X POST "$BASE/api/tags" -H 'Content-Type: application/json' -d '{"name":"演示标签"}' -w '  ← HTTP %{http_code}\n'

# ---------------------------------------------------------------------------
step "6. 标题前缀搜索（GET /api/search?q=三体）"
req "$BASE/api/search?q=三体" | jq '{total, results: [.items[] | {id, title, score}]}'

# ---------------------------------------------------------------------------
step "7. 相似推荐：与《$TARGET_TITLE》相似（GET /api/recommend?item_id=$TARGET_ID&topN=3）"
req "$BASE/api/recommend?item_id=$TARGET_ID&topN=3" | jq '{items: [.items[] | {id, title, score}]}'

step "8. 冷启动高分榜（GET /api/recommend?topN=3，不带 item_id）"
req "$BASE/api/recommend?topN=3" | jq '{items: [.items[] | {id, title, score}]}'

# ---------------------------------------------------------------------------
step "9. 统计聚合（GET /api/stats 与 ?type=book）"
req "$BASE/api/stats" | jq '{total, by_type}'
req "$BASE/api/stats?type=book" | jq '{total, score_dist}'

# ---------------------------------------------------------------------------
step "10. 错误语义：非法参数 400、不存在 404"
printf 'items?type=game → HTTP %s\n' "$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$BASE/api/items?type=game")"
printf 'items/999999    → HTTP %s\n' "$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$BASE/api/items/999999")"
printf 'recommend?item_id=999999 → HTTP %s\n' "$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$BASE/api/recommend?item_id=999999")"

# ---------------------------------------------------------------------------
step "11. 写路径：新增 → 修改 → 删除 → 撤销删除 = 新 id"
CREATED="$(req -X POST "$BASE/api/items" -H 'Content-Type: application/json' \
    -d '{"type":"book","title":"演示条目","author":"演示作者","score":60,"tags":["演示标签"]}')"
NEW_ID="$(printf '%s' "$CREATED" | jq -r '.id')"
printf 'POST   → HTTP 201，id=%s\n' "$NEW_ID"

req -X PUT "$BASE/api/items/$NEW_ID" -H 'Content-Type: application/json' \
    -d '{"score":100,"comment":"演示更新"}' | jq '{id, score, comment, updated_at}'

printf 'DELETE → HTTP %s\n' "$(curl -s -m 5 -o /dev/null -w '%{http_code}' -X DELETE "$BASE/api/items/$NEW_ID")"
printf '删除后 GET → HTTP %s（404 = 已不存在）\n' "$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$BASE/api/items/$NEW_ID")"

REBUILT_ID="$(req -X POST "$BASE/api/items" -H 'Content-Type: application/json' \
    -d '{"type":"book","title":"演示条目","author":"演示作者","score":60,"tags":["演示标签"]}' | jq -r '.id')"
printf '重新创建（客户端撤销删除的语义）→ 新 id=%s（原 id=%s 不复用）\n' "$REBUILT_ID" "$NEW_ID"
curl -s -m 5 -o /dev/null -X DELETE "$BASE/api/items/$REBUILT_ID"

# ---------------------------------------------------------------------------
step "演示结束"
if [[ "$MANAGED" -eq 1 ]]; then
    if [[ "$KEEP" -eq 1 ]]; then
        echo "临时数据保留在: $WORK/db.json（服务端已停止）"
        WORK=""
    else
        echo "临时实例与数据随脚本退出自动清理（--keep 可保留）。"
    fi
else
    echo "注意：--url 模式会在目标实例上创建/删除演示条目，并留下一个孤儿标签「演示标签」（属正常语义）。"
fi
