#!/bin/bash
make clean;
make;
rm -rf img;
echo ----------------------------;
echo "Executing commands";
# create an image
truncate -s 10MiB img;
# format the image
./mkfs.a1fs -i 4096 img;
# mount the image
./a1fs img /tmp/chenxuyu;
cd /tmp/chenxuyu;
# make a directory
mkdir alan;
# creat a file
touch tony;
# add data to a file (Keep it simple)
echo "This is test message" >> tony;
# display all attributes of files and directories
ls -al;
echo ----------------------------;
echo "Unmount";
# unmount the image
cd ..;
fusermount -u /tmp/chenxuyu;
# mount the image again and display some contents of the file system to show that the relevant state was saved to the disk image.
echo ----------------------------;
echo "Remount";
cd ~/a1b;
./a1fs img /tmp/chenxuyu;
cd /tmp/chenxuyu;
# display all attributes of files and directories
ls -al;
cd ..;
fusermount -u /tmp/chenxuyu;
cd ~/a1b;