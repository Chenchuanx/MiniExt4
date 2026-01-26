#!/bin/bash
set -e

# 项目根目录
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

echo "[MiniExt4] 使用 CMake 编译内核..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 生成构建文件并导出 compile_commands.json
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# 编译内核和 ISO
make kernel.bin
make kernel.iso

echo "[MiniExt4] 编译完成:"
echo "  kernel.bin -> $BUILD_DIR/kernel.bin"
echo "  kernel.iso -> $BUILD_DIR/kernel.iso"
