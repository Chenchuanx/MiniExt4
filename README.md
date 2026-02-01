# MiniExt4

## 编译
    ./build.sh

## 运行
    dd if=/dev/zero of=ext4_disk.img bs=1M count=64 2>/dev/null

    qemu-system-i386 -cdrom build/kernel.iso -hda ext4_disk.img -boot d -m 512M -serial stdio -display gtk