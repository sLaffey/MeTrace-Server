#!/bin/bash
# 接口冒烟测试。先启动服务端（./scripts/run.sh），再执行：
#   ./tests/test_api.sh [base_url]
#
# 依赖：curl、grep。测试会往数据库里写入数据，建议用临时库：
#   ./scripts/run.sh --db /tmp/mediatrace-test.db
set -uo pipefail

BASE="${1:-http://127.0.0.1:8000}"
PASS=0
FAIL=0

# check <描述> <期望> <实际>
check() {
    if [[ "$2" == "$3" ]]; then
        echo "  ok   $1"
        PASS=$((PASS + 1))
    else
        echo "  FAIL $1 (期望 [$2] 实际 [$3])"
        FAIL=$((FAIL + 1))
    fi
}

# 只取 HTTP 状态码
status() {
    curl -s -o /dev/null -w '%{http_code}' "$@"
}

json_field() { # json_field <字段名> <json>
    printf '%s' "$2" | grep -o "\"$1\":[^,}]*" | head -n1 | cut -d: -f2- | tr -d '"'
}

echo "== 健康检查 =="
check "GET /ping 返回 200" 200 "$(status "$BASE/ping")"

echo "== 新增条目 =="
CREATE_BODY='{"type":"book","title":"三体","creator":"刘慈欣","year":2006,"status":"doing","progress":0.5,"score":9.5,"review":"震撼","tags":["科幻","中国文学"]}'
RESP=$(curl -s -X POST "$BASE/api/items" -H 'Content-Type: application/json' -d "$CREATE_BODY")
POST_STATUS=$(status -X POST "$BASE/api/items" -H 'Content-Type: application/json' -d '{"type":"movie","title":"星际穿越"}')
ITEM_ID=$(json_field id "$RESP")
CREATED_AT=$(json_field created_at "$RESP")
check "POST /api/items 返回 201" 201 "$POST_STATUS"
check "响应含 title" "三体" "$(json_field title "$RESP")"
check "响应含 score" "9.5" "$(json_field score "$RESP")"
check "响应含标签 科幻" "true" "$(printf '%s' "$RESP" | grep -q '科幻' && echo true || echo false)"
echo "     (新条目 id=$ITEM_ID, created_at=$CREATED_AT)"

echo "== 查询条目 =="
check "GET /api/items/$ITEM_ID 返回 200" 200 "$(status "$BASE/api/items/$ITEM_ID")"
check "GET /api/items/999999 返回 404" 404 "$(status "$BASE/api/items/999999")"
check "GET /api/items/abc 返回 404（不匹配数字路由）" 404 "$(status "$BASE/api/items/abc")"
LIST=$(curl -s "$BASE/api/items?type=book&sort=-score&limit=10")
check "GET /api/items 含 total 字段" "true" "$(printf '%s' "$LIST" | grep -q '"total"' && echo true || echo false)"
check "GET /api/items 含 items 数组" "true" "$(printf '%s' "$LIST" | grep -q '"items"' && echo true || echo false)"
check "按标签过滤" 200 "$(status "$BASE/api/items?tag=科幻")"
check "非法 type 返回 400" 400 "$(status "$BASE/api/items?type=game")"
check "非法 sort 返回 400" 400 "$(status "$BASE/api/items?sort=unknown")"

echo "== 更新条目 =="
sleep 1 # datetime('now') 只精确到秒，等一秒才能看出 updated_at 是否真的刷新
UPDATE_BODY='{"status":"done","progress":1,"score":9.8}'
UPD=$(curl -s -X PUT "$BASE/api/items/$ITEM_ID" -H 'Content-Type: application/json' -d "$UPDATE_BODY")
UPDATED_AT=$(json_field updated_at "$UPD")
check "PUT 返回 200" 200 "$(status -X PUT "$BASE/api/items/$ITEM_ID" -H 'Content-Type: application/json' -d "$UPDATE_BODY")"
check "status 已更新为 done" "done" "$(json_field status "$UPD")"
check "progress 已更新为 1" "1.0" "$(json_field progress "$UPD")"
check "created_at 未被改动" "$CREATED_AT" "$(json_field created_at "$UPD")"
check "updated_at 已刷新" "true" "$([[ -n "$UPDATED_AT" && "$UPDATED_AT" != "$CREATED_AT" ]] && echo true || echo false)"
CLEARED=$(curl -s -X PUT "$BASE/api/items/$ITEM_ID" -H 'Content-Type: application/json' -d '{"score":null}')
check "传 null 可清空 score" "null" "$(json_field score "$CLEARED")"
check "PUT 不存在的 id 返回 404" 404 "$(status -X PUT "$BASE/api/items/999999" -H 'Content-Type: application/json' -d '{"title":"x"}')"

echo "== 参数校验 =="
check "缺 title 返回 400" 400 "$(status -X POST "$BASE/api/items" -H 'Content-Type: application/json' -d '{"type":"book"}')"
check "非法 type 返回 400" 400 "$(status -X POST "$BASE/api/items" -H 'Content-Type: application/json' -d '{"type":"game","title":"x"}')"
check "progress 越界返回 400" 400 "$(status -X POST "$BASE/api/items" -H 'Content-Type: application/json' -d '{"type":"book","title":"x","progress":2}')"
check "score 越界返回 400" 400 "$(status -X POST "$BASE/api/items" -H 'Content-Type: application/json' -d '{"type":"book","title":"x","score":99}')"
check "请求体非法 JSON 返回 400" 400 "$(status -X POST "$BASE/api/items" -H 'Content-Type: application/json' -d 'not json')"
# 没有"空"状态的字段不允许传 null，否则会被静默改成默认值
check "PUT type=null 返回 400" 400 "$(status -X PUT "$BASE/api/items/$ITEM_ID" -H 'Content-Type: application/json' -d '{"type":null}')"
check "PUT status=null 返回 400" 400 "$(status -X PUT "$BASE/api/items/$ITEM_ID" -H 'Content-Type: application/json' -d '{"status":null}')"
check "PUT progress=null 返回 400" 400 "$(status -X PUT "$BASE/api/items/$ITEM_ID" -H 'Content-Type: application/json' -d '{"progress":null}')"
check "PUT title 空串返回 400" 400 "$(status -X PUT "$BASE/api/items/$ITEM_ID" -H 'Content-Type: application/json' -d '{"title":""}')"
check "校验失败不改动数据" "book" "$(json_field type "$(curl -s "$BASE/api/items/$ITEM_ID")")"

echo "== 标签 =="
check "GET /api/tags 返回 200" 200 "$(status "$BASE/api/tags")"
check "POST /api/tags 返回 201" 201 "$(status -X POST "$BASE/api/tags" -H 'Content-Type: application/json' -d '{"name":"科幻"}')"
check "POST /api/tags 缺 name 返回 400" 400 "$(status -X POST "$BASE/api/tags" -H 'Content-Type: application/json' -d '{}')"

echo "== 删除条目 =="
check "DELETE 返回 204" 204 "$(status -X DELETE "$BASE/api/items/$ITEM_ID")"
check "删除后 GET 返回 404" 404 "$(status "$BASE/api/items/$ITEM_ID")"
check "重复 DELETE 返回 404" 404 "$(status -X DELETE "$BASE/api/items/$ITEM_ID")"

echo "== 未知路由 =="
check "GET /api/nope 返回 404" 404 "$(status "$BASE/api/nope")"

echo
echo "通过 $PASS 项，失败 $FAIL 项"
[[ "$FAIL" -eq 0 ]]
