#!/bin/bash

chroot /target /socat tcp-listen:1337,fork,reuseaddr exec:"/qemu /bin/su ctf -c \\'cd /ctf && /qemu ./chal\\'",stderr