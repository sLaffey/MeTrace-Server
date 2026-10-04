#!/bin/bash
# 启动服务端。默认 127.0.0.1:8000，数据库 ./data/metrace.json（目录会自动创建）。
# 额外参数会透传给程序，例如：./scripts/run.sh --port 9000 --db /tmp/demo.json
set -e
cd "$(dirname "$0")/.."

exec ./build/bin/server "$@"
