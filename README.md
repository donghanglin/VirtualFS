# CS-GY-6233
Introduction to Operating Systems I

## [Virtual File System](https://github.com/donghanglin/CS-GY-6233/blob/master/vfs.c)
It is a file-based file system which simulates a file system working with block devices. The essential data structures of this file system include superblock, inodes and free block lists. This file system is hierarchical. It supports files and multilayer directories. 

- **Environment**  
  Ubuntu 14.04.2 LTS
- **Dependency**
  - [FUSE](fuse.sourceforge.net): 2.9.3
- **Compile**  

  ```sh
  gcc -Wall vfs.c `pkg-config fuse --cflags --libs` -o vfs
  ```
- **Mount**

  ```sh
  ./vfs /tmp/fuse
  ```
  - You need root privilege to mount this file system
- **Supported Linux command**  
  `touch`, `mkdir`, `echo`, `cat`, `ln`, `rm`, `rm -r`, `mv`, `cp`, `df`

## [File System Checker](https://github.com/donghanglin/CS-GY-6233/blob/master/fsck.py)
It is a simulated Linux file system checker which can find and correct potential errors existing in the file-based file system.
