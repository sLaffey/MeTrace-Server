#!/bin/bash
# 配置并编译 MeTrace-Server。
# 注意：首次执行需要联网，FetchContent 会把依赖拉到 build/_deps/ 下并缓存起来。
set -e
cd "$(dirname "$0")/.."

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

echo "构建完成: $(pwd)/build/bin/server"
