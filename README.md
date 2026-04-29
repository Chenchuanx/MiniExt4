# MiniExt4

## 编译
    ./build.sh

## 运行
    dd if=/dev/zero of=ext4_disk.img bs=1M count=64 2>/dev/null && qemu-system-i386 -cdrom build/kernel.iso -hda ext4_disk.img -boot d -m 512M -serial stdio -display gtk

## 镜像
    dd if=/dev/zero of=ext4_disk.img bs=1M count=64 && mkfs.ext4 -F -b 4096 ext4_disk.img

## 挂载
    sudo mount -o loop ext4_disk.img ./mnt