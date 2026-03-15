#!/bin/bash
#
# 在 Linux 下创建用于 MiniExt4 测试的 ext4 镜像
# 镜像使用 4KB 块、禁用 extents，与项目内 ext4 实现兼容
#

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE_PATH="${1:-$PROJECT_ROOT/ext4_disk.img}"
MNT="${2:-$PROJECT_ROOT/mnt}"
SIZE_MB=64

echo "=== MiniExt4 测试镜像创建脚本 ==="
echo "镜像路径: $IMAGE_PATH"
echo "挂载点:   $MNT"
echo "大小:     ${SIZE_MB}MB"
echo ""

# 1. 创建空镜像（与 README 一致）
echo "[1/5] 创建 ${SIZE_MB}MB 空镜像..."
dd if=/dev/zero of="$IMAGE_PATH" bs=1M count=$SIZE_MB 2>/dev/null

# 2. 格式化为 ext3（传统块映射，无 extents；新版 mke2fs 要求 64 位 ext4 必须开 extents，故用 ext3）
echo "[2/5] 格式化为 ext3（4KB 块，传统 i_block 映射）..."
mke2fs -t ext3 -b 4096 -F "$IMAGE_PATH" >/dev/null 2>&1

# 3. 创建挂载点并挂载
echo "[3/5] 挂载镜像..."
mkdir -p "$MNT"
sudo umount "$MNT" 2>/dev/null || true
sudo mount -o loop "$IMAGE_PATH" "$MNT"

# 4. 在镜像内创建测试目录和文件
echo "[4/5] 创建测试目录和文件..."
sudo mkdir -p "$MNT/testdir"
sudo mkdir -p "$MNT/hello"
sudo sh -c "echo 'Hello from Linux ext4' > '$MNT/hello.txt'"
sudo sh -c "echo 'content in subdir' > '$MNT/testdir/subfile.txt'"
sudo sh -c "echo 'another file' > '$MNT/hello/world.txt'"
# 确保数据落盘
sync

# 5. 卸载
echo "[5/5] 卸载镜像..."
sudo umount "$MNT"
echo ""
echo "完成。镜像已生成: $IMAGE_PATH"
echo ""
echo "用本项目加载此镜像："
echo "  ./build.sh"
echo "  qemu-system-i386 -cdrom build/kernel.iso -hda $IMAGE_PATH -boot d -m 512M -serial stdio -display gtk"
echo ""
echo "在 QEMU 中可尝试列出根目录或读取文件，验证是否能识别 Linux 创建的内容。"
