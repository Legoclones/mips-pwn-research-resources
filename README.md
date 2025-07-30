# MIPS Pwn Research Resources
During my time researching MIPS binary exploitation, I created some resources that were super helpful for me and thought I'd share publicly for everyone to use. They include:

* [60 Docker containers for cross-compiling C/C++ code](#cross-compilation-containers)
* [Static, patched, userspace versions of QEMU 9.2 that supports ASLR](#userspace-qemu-binaries)
* [60 Docker containers for emulating MIPS binaries](#emulation-containers)
* [MIPS system QEMU 9.2 binaries for kernel emulation](#system-qemu-binaries)
* [Emulatable mipsel32r2 Linux kernel version 6.15.2 using Malta board](#linux-kernel)
* [Official MIPS specs, design documents, and other useful resources](#mips-documents)

## Cross-Compilation Containers
> *[See these containers on Docker Hub](https://hub.docker.com/repository/docker/legoclones/mips-compile/general)*

I created over 60 Docker containers to cross-compile C/C++ code from a 64-bit x86 machine into one of the supported MIPS architectures, avoiding installing buildroot and compiling the dependencies on your own. I support all releases of MIPS32 and MIPS64 using glibc, musl, and uClibc-ng (see all tags [on DockerHub](https://hub.docker.com/repository/docker/legoclones/mips-compile/general)).

### Usage
These containers should usually be used on the command line (and not from a Dockerfile, although I guess they can be):
```bash
docker run -it --rm -v ${PWD}:/workdir legoclones/mips-compile:<build_id> <command>
```

As an example, if you wanted to compile `file.c` in your current directory using `gcc` to make a little endian MIPS32r2 binary, you'd run:
```bash
docker run -it --rm -v ${PWD}:/workdir legoclones/mips-compile:mipsel32r2-glibc mipsel-linux-gcc file.c -o file
```

### Building
If you'd like to follow the same process to build these containers yourself, I've attached my command below. Assuming [Buildroot](https://buildroot.org/download.html) has already been downloaded, [configured, and the toolchain compiled](https://buildroot.org/downloads/manual/manual.html#_buildroot_quick_start), and assuming an existing `./docker/compiling/<build_id>/` folder exists, build the Docker container using the following command:
```bash
docker build -f docker/Dockerfile.cc . -t mips-compile:<build_id> --build-arg build=<build_id>
```

## Userspace QEMU Binaries
QEMU is a popular emulation framework that [supports various user-mod MIPS versions and releases](https://qemu-project.gitlab.io/qemu/user/main.html#other-binaries) (MIPS32 and MIPS64). However, QEMU doesn't support ASLR out-of-the-box, making it difficult to use in security-conscious contexts like CTF problems or fuzzing. Therefore, I wrote [a custom patch](./qemu/aslr.patch) for QEMU 9.2 that enables ASLR on all running binaries, using 12 bits of randomness for 32-bit binaries and 36 bits of randomness for 64-bit binaries. These QEMU binaries enable stack canaries, PIE, RELRO, and DEP.

The binaries were compiled (and can be replicated by you) inside of an `ubuntu:24.04` Docker container using the following commands:
```bash
# install dependencies
apt update
apt install -y gcc make file wget cpio unzip python3 python3-venv python3-pip git libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev ninja-build
pip3 install --break-system-packages sphinx sphinx_rtd_theme

# get code
cd /tmp
git clone https://github.com/qemu/qemu
cd qemu
git checkout stable-9.2

# patch for ASLR
cp qemu/aslr.patch linux-user/aslr.patch                # the aslr.patch file is custom and located in this repo
cd linux-user && patch mmap.c aslr.patch && cd ../

# build
mkdir build
cd build
../configure --target-list=mips-linux-user,mips64-linux-user,mips64el-linux-user,mipsel-linux-user,mipsn32-linux-user,mipsn32el-linux-user --static
make
```

Note that there are 6 (statically-compiled) executables resulting from this, supporting N32 and O32 ABIs on 32-bit MIPS in addition to 64-bit MIPS. They can be downloaded [from the `qemu/` folder](./qemu/):
* [`qemu-mips`](./qemu/qemu-mips) (32-bit, big endian, O32 ABI)
* [`qemu-mipsel`](./qemu/qemu-mipsel) (32-bit, little endian, O32 ABI)
* [`qemu-mips64`](./qemu/qemu-mips64) (64-bit, big endian, N64 ABI)
* [`qemu-mips64el`](./qemu/qemu-mips64el) (64-bit, little endian, N64 ABI)
* [`qemu-mipsn32`](./qemu/qemu-mipsn32) (32-bit, big endian, N32 ABI)
* [`qemu-mipsn32el`](./qemu/qemu-mipsn32el) (32-bit, little endian, N32 ABI)

*(Note - also for funzies I included a version of `qemu-mipsel` with no ASLR in the same directory)*

## Emulation Containers
> *[See these containers on Docker Hub](https://hub.docker.com/repository/docker/legoclones/mips-pwn/general)*

While the userspace QEMU binaries can emulate any cross-compiled MIPS binaries out of the box, any dynamically-compiled binaries still need the libraries present on the system, and any MIPS pwn CTF problems require a bit more work. Therefore, I created a 60 containers that embed the userspace QEMU binaries (described above) into an Ubuntu 24.04 container with the target MIPS libraries pre-installed. Just as with the cross-compilation containers, I support all releases of MIPS32/64 and the libc implementations glibc, musl, and uClibc-ng (see all tags [on DockerHub](https://hub.docker.com/repository/docker/legoclones/mips-pwn/general)).

All QEMU containers perform userspace emulation using statically-compiled QEMU with a custom patch to insert ASLR. The `/target/` directory is used as the chroot directory for emulation. The entire `output/target/` directory from the corresponding MIPS buildroot is copied over to the Docker container, along with the proper variation of QEMU as specified by Docker build arguments.

Ingress to the container is handled by (statically-compiled) socat, which handles concurrent TCP connections. It first drops privileges to an unprivileged user, then runs `/ctf/chal`. In order to handle exploits that use the `execve` syscall to call `/bin/sh`, the executables `sh`, `cat`, and `ls` are copied into the chroot environment. These 3 executables are patched and their `RPATH` is set to `/x86`, where necessary x86_64 libraries are copied. This is done so that MIPS and x86 libraries are effectively separated and all remotely-spawned processes are unprivileged.

**TL;DR**:
* QEMU is patched with ASLR
* x86 version of `sh`, `cat`, and `ls` are available for easy access in exploits
* All remotely-spawned processed are unprivileged even inside the chroot environment

### Usage
#### CTF Hosting
Challenge files are intended to be placed in `/ctf`, with the challenge binary located at `/ctf/chal`. An example Dockerfile that would import CTF-specific files into one of these QEMU containers is below:

```dockerfile
FROM legoclones/mips-pwn:<build_id>

# copy files
COPY ctf/ /target/ctf

# run
CMD ["bash", "/start.sh"]
EXPOSE 1337
```

Normally, the only two files inside the `./ctf/` folder are `chal` and `flag.txt`.

#### Running and Debugging
To simply run the executable with QEMU in an already containerized environment with the needed libraries, use the following one-liner:

```bash
docker run -it --rm -v ${PWD}:/target/ctf legoclones/mips-pwn:<build_id> chroot /target /qemu /ctf/<file_in_current_dir>
```

To spawn a remote debugging session using GDB on port 1234, use the following one-liner:
```bash
docker run -it --rm -v ${PWD}:/target/ctf -p 1234:1234 legoclones/mips-pwn:<build_id> chroot /target /qemu -g 1234 /ctf/<file_in_current_dir>
```

### Building
If you'd like to follow the same process to build these containers yourself, I've attached my command below. Assuming [Buildroot](https://buildroot.org/download.html) has already been downloaded, [configured, and the toolchain compiled](https://buildroot.org/downloads/manual/manual.html#_buildroot_quick_start), and assuming an existing `./docker/target/<build_id>/` folder exists, build the Docker container using the following command:
```bash
docker build -f docker/Dockerfile.qemu . -t legoclones/mips-pwn:<build_id> --build-arg build=<build_id> --build-arg qemu_ver=<path_to_compiled_qemu>
```

## System QEMU Binaries
In order to emulate MIPS CPUs for my own Linux kernels, I also compiled QEMU system binaries using version 9.2.

The binaries were compiled (and can be replicated by you) inside of an `ubuntu:24.04` Docker container using the following commands:
```bash
# install dependencies
apt update
apt install -y gcc make file wget cpio unzip python3 python3-venv python3-pip git libglib2.0-dev libfdt-dev libpixman-1-dev zlib1g-dev ninja-build
pip3 install --break-system-packages sphinx sphinx_rtd_theme

# get code
cd /tmp
git clone https://github.com/qemu/qemu
cd qemu
git checkout stable-9.2

# build
mkdir build
cd build
../configure --target-list=mips-softmmu,mips64-softmmu,mips64el-softmmu,mipsel-softmmu
make
```

There are 4 (dynamically-linked) binaries resulting from this:
* [`qemu-system-mips`](./qemu/qemu-system-mips) (32-bit, big endian)
* [`qemu-system-mipsel`](./qemu/qemu-system-mipsel) (32-bit, little endian)
* [`qemu-system-mips64`](./qemu/qemu-system-mips64) (64-bit, big endian)
* [`qemu-system-mips64el`](./qemu/qemu-system-mips64el) (64-bit, little endian)

*(Note - for some reason, my machine would occasionally complain that files like `efi-pcnet.rom` weren't available, so I copied those into a [`roms/`](./qemu/roms/) folder. If you run into the same issue, just have that file in the current directory and you should be good to go)*

## Linux Kernel
I wanted my own Linux kernel in MIPS that wasn't decades old to use in some research, so I compiled my own mipsel32r2 Linux version 6.15.2 kernel that can be successfully emulated by QEMU system binaries and a rootfs from Buildroot.

### Building the Linux Kernel
> [`vmlinux`](./kernel/vmlinux) and [`vmlinuz`](./kernel/vmlinuz)

I copied the latest kernel version (6.15.2 at the time) to my machine and use a pre-defined kernel config for the Malta board (`malta_defconfig`) inside of my `legoclones/mips-compile:latest` Docker container:
```bash
# set options to cross-compile
export ARCH=mips
export CROSS_COMPILE=mipsel-linux-

# set up
cd /tmp
apt update
apt install -y wget git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison u-boot-tools
tar -xf linux-6.15.2.tar.xz

# compile
cd linux-6.15.2
make malta_defconfig
make -j$(nproc)
```

Linux has SEVERAL configurations for MIPS kernels right out of the gate (see `make help`), but the Malta board seems to have the best support in QEMU so I chose that profile. Note that you can further customize options if you wish by running `make menuconfig`.

### Creating the rootfs
> [`rootfs.cpio`](./kernel/rootfs.cpio)

In addition to the QEMU emulator and kernel, a root filesystem is needed so there's something to run once the kernel is up. The containers above were built using [Buildroot](https://buildroot.org/download.html), so I made sure to also create a `cpio` image when [compiling](https://buildroot.org/downloads/manual/manual.html#_buildroot_quick_start) for `mipsel32r2-glibc` (and found it in `output/images/`).

The toolchain was built with Buildroot in an `ubuntu:24.04` Docker container with the following commands (after downloading/decompressing [the package](https://buildroot.org/download.html)):
```bash
apt install -y gcc make sudo file g++ wget cpio unzip rsync bc bzip2 patch perl libncurses-dev
make menuconfig # this is where you set options for the toolchain
make
```

### Kernel Emulation
Once you have the [QEMU system binary](./qemu/), the [kernel `vmlinux`](./kernel/vmlinux) (or [`vmlinuz`](./kernel/vmlinuz)), and the [rootfs](./kernel/rootfs.cpio), you can emulate the system with the following command:
```bash
./qemu/qemu-system-mipsel -M malta -kernel ./kernel/vmlinux -nographic -m 256M -append "console=ttyS0" -initrd ./kernel/rootfs.cpio
```

<img src="./image.png">

## MIPS Documents
* [MIPS-specific GCC compiler options](https://gcc.gnu.org/onlinedocs/gcc/MIPS-Options.html)
* **MIPS Architecture for Programmers (AFP)** - a set of volumes describing the MIPS specification
    * **MIPS32/microMIPS32**
        * Volume I-A: Introduction to the MIPS32 Architecture ([rev 6.01](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00082-2B-MIPS32INT-AFP-06.01.pdf))
        * Volume I-B: Introduction to the microMIPS32 Architecture ([rev 5.03](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00741-2B-microMIPS32INT-AFP-05.03.pdf), [rev 6.00](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00741-2B-microMIPS32INT-AFP-06.00.pdf))
        * Volume II-A: The MIPS32 Instruction Set Manual ([rev 5.04](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00086-2B-MIPS32BIS-AFP-05.04.pdf), [rev 6.06](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00086-2B-MIPS32BIS-AFP-6.06.pdf))
        * Volume II-B: microMIPS32 Instruction Set ([rev 5.04](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00582-2B-microMIPS32-AFP-05.04.pdf), [rev 6.05](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00582-2B-microMIPS32-AFP-6.05.pdf))
        * Volume III: MIPS32/microMIPS32 Privileged Resource Architecture ([rev 5.05](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00090-2B-MIPS32PRA-AFP-05.05.pdf), [rev 6.02](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00090-2B-MIPS32PRA-AFP-06.02.pdf))
        * Volume IV-a: The MIPS16e Application Specific Extension to the MIPS32 Architecture ([rev 2.63](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00076-2B-MIPS1632-AFP-02.63.pdf))
        * Volume IV-e: MIPS DSP Module for MIPS32 Architecture ([rev 3.01](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00374-2B-MIPS32DSP-AFP-03.01.pdf))
        * Volume IV-e: MIPS DSP Module for microMIPS32 Architecture ([rev 3.01](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00764-2B-microMIPS32DSP-AFP-03.01.pdf))
        * Volume IV-f: The MIPS MT Module for the MIPS32 Architecture ([rev 1.12](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00378-2B-MIPS32MT-AFP-01.12.pdf))
        * Volume IV-f: The MIPS MT Module for the microMIPS32 Architecture ([rev 1.12](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00768-1C-microMIPS32MT-AFP-01.12.pdf))
        * Volume IV-h: The MCU Application Specific Extension to the MIPS32 Architecture ([rev 1.03](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00834-2B-MUCON-AFP-01.03.pdf))
        * Volume IV-h: The MCU Application Specific Extension to the microMIPS32 Architecture ([rev 1.03](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00838-2B-microMIPS32MUCON-AFP-01.03.pdf))
        * Volume IV-i: Virtualization Module of the MIPS32 Architecture ([rev 1.06](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00846-2B-VZMIPS32-AFP-01.06.pdf))
        * Volume IV-i: Virtualization Module of the microMIPS32 Architecture ([rev 1.06](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00848-2B-VZmicroMIPS32-AFP-01.06.pdf))
        * Volume IV-j: The MIPS32 SIMD Architecture Module ([rev 1.12](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00866-2B-MSA32-AFP-01.12.pdf))
        * MIPS16e2 Application-Specific Extension Technical Reference Manual ([rev 1.00](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD01172-2B-MIPS16e2-AFP-01.00.pdf))
    * **MIPS64/microMIPS64**
        * Volume I-A: Introduction to the MIPS64 Architecture ([rev 5.04](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00083-2B-MIPS64INT-AFP-05.04.pdf), [rev 6.01](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00083-2B-MIPS64INT-AFP-06.01.pdf))
        * Volume I-B: Introduction to the microMIPS64 Architecture ([rev 5.03](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00743-2B-microMIPS64INT-AFP-05.03.pdf))
        * Volume II-A: The MIPS64 Instruction Set Reference Manual ([rev 5.04](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00087-2B-MIPS64BIS-AFP-05.04.pdf), [rev 6.06](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00087-2B-MIPS64BIS-AFP-6.06.pdf))
        * Volume II-B: microMIPS64 Instruction Set ([rev 5.04](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00594-2B-microMIPS64-AFP-05.04.pdf), [rev 6.05](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00594-2B-microMIPS64-AFP-6.05.pdf))
        * Volume III: The MIPS64 and microMIPS64 Privileged Resource Architecture ([rev 5.04](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00091-2B-MIPS64PRA-AFP-05.04.pdf), [rev 6.03](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00091-2B-MIPS64PRA-AFP-06.03.pdf))
        * Volume IV-c: The MIPS-3D Application-Specific Extension to the MIPS64 Architecture ([rev 2.50](https://0x04.net/~mwk/doc/mips/MD00099-2B-MIPS3D64-AFP-02.50.pdf))
        * Volume IV-d: The SmartMIPS Application-Specific Extension to the MIPS32 Architecture ([rev 3.01](http://www.t-es-t.hu/download/mips/md00101c.pdf))
        * Volume IV-e: MIPS DSP Module for MIPS64 Architecture ([rev 3.02](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00375-2B-MIPS64DSP-AFP-03.02.pdf))
        * Volume IV-e: MIPS DSP Module for microMIPS64 Architecture ([rev 3.02](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00765-2B-microMIPS64DSP-AFP-03.02.pdf))
        * Volume IV-i: Virtualization Module of the MIPS64 Architecture ([rev 1.06](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00847-2B-VZMIPS64-AFP-01.06.pdf))
        * Volume IV-i: Virtualization Module of the microMIPS64 Architecture ([rev 1.06](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00849-2B-VZmicroMIPS64-AFP-01.06.pdf))
        * Volume IV-j: The MIPS64 SIMD Architecture Module ([rev 1.12](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00868-1D-MSA64-AFP-01.12.pdf))
* **Software Users Manuals**
    * [MIPS32 4K Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00016-2B-4K-SUM-01.18.pdf) (rev 1.18)
    * [MIPS32 M4K Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00249-2B-M4K-SUM-02.03.pdf) (rev 2.03)
    * [MIPS32 M14K Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00668-2B-M14K-SUM-02.04.pdf) (rev 2.04)
    * [MIPS32 M14Kc Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00674-2B-M14Kc-SUM-02.04.pdf) (rev 2.04)
    * [MIPS32 24K Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00343-2B-24K-SUM-03.11.pdf) (rev 3.11)
    * [MIPS32 34K Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00534-2B-34K-SUM-01.13.pdf) (rev 1.13)
    * [MIPS32 74K Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00519-2B-74K-SUM-01.05.pdf) (rev 1.05)
    * [MIPS32 1004K Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00622-2B-1004K-SUM-01.21.pdf) (rev 1.21)
    * [MIPS32 1074K Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00749-2B-1074K-SUM-01.03.pdf) (rev 1.03)
    * [MIPS32 M5100 Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS_Warrior_M5100_SoftwareUserManual_MD00964_01.04.pdf) (rev 1.04)
    * [MIPS32 M5150 Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS_Warrior_M5150_SoftwareUserManual_MD00980_01.05.pdf) (rev 1.05)
    * [MIPS32 proAptiv Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00878-2B-proAptiv-SUM-01.22.pdf) (rev 1.22)
    * [MIPS32 interAptiv Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00904-2B-interAptiv-SUM-02.01.pdf) (rev 2.01)
    * [MIPS32 microAptiv UC Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00934-2B-microAptivUC-SUM-01.03.pdf) (rev 1.03)
    * [MIPS32 microAptiv UP Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00942-2B-microAptivUP-SUM-01.02.pdf) (rev 1.02)
    * [MIPS32 P5600 Software Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS-MD01025-2B-P5600-Software-TRM-01.60.pdf) (rev 1.60)
    * [MIPS64 P6600 Multiprocessing System Software Users Guide](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS_Warrior_P6600_SoftwareUserGuide_MD01138_P_01.23.pdf) (rev 1.23)
* **CPU Datasheets**
    * [MIPS32 M4K Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00247-2B-M4K-DTS-02.01.pdf)
    * [MIPS32 M14K Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00666-2B-M14K-DTS-02.05.pdf)
    * [MIPS32 M14Kc Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00672-2B-M14Kc-DTS-02.05.pdf)
    * [MIPS32 24Kc Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00346-2B-24K-DTS-04.00.pdf)
    * [MIPS32 24Kf Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00354-2B-24K-DTS-04.00.pdf)
    * [MIPS32 34Kc Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00418-2B-34Kc-DTS-01.21.pdf)
    * [MIPS32 34Kf Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00419-2B-34Kf-DTS-01.20.pdf)
    * [MIPS32 74Kc Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00496-2B-74KC-DTS-01.07.pdf)
    * [MIPS32 74Kf Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00497-2B-74KF-DTS-01.07.pdf)
    * [MIPS32 1004K Coherent Processing System Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00584-2B-1004K-DTS-01.20.pdf)
    * [MIPS32 1074K Coherent Processing System Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00745-2B-CMP-DTS-01.03.pdf)
    * [MIPS32 M5100 Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00961-2B-M5100-DTS-01.00.pdf)
    * [MIPS32 M5150 Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00977-2B-M5150-DTS-01.01.pdf)
    * [MIPS32 M6200 Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD01092-2B-M6200-DTS-01.00.pdf)
    * [MIPS32 M6200 Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS32_MD01092_M6200_Datasheet_01.00.pdf)
    * [MIPS32 M6250 Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD01098-2B-M6250-DTS-01.00.pdf)
    * [MIPS32 proAptiv Multiprocessing System Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00877-2B-proAptiv-DTS-01.02.pdf)
    * [MIPS32 interAptiv Multiprocessing System Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00903-2B-interAptiv-DTS-01.20.pdf)
    * [MIPS32 microAptiv UC Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00931-2B-microAptivUC-DTS-01.01.pdf)
    * [MIPS32 microAptiv UP Processor Core Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00939-2B-microAptivUP-DTS-01.00.pdf)
    * [MIPS32 P5600 Multiprocessing System Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD01024-2B-P5600-DTS-01.00.pdf)
    * [MIPS64 P6600 Multiprocessing System Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS_Warrior_P6600_Datasheet_MD01137_P_01.02.pdf)
    * [MIPS64 I6500 Multiprocessing System Datasheet](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS_Warrior_I6500_%20Datasheet%20_MD01174_P_1.00.pdf)
* **CPU Programming Guides**
    * [Programming the MIPS32 24K Core Family](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00355-2B-24KPRG-PRG-04.63.pdf) (rev 4.63)
    * [Programming the MIPS32 34K Core Family](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00427-2B-34K-PRG-01.64.pdf) (rev 1.64)
    * [Programming the MIPS32 74K Core Family](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00541-2B-74K-PRG-02.14.pdf) (rev 2.14)
    * [Programming the MIPS 74K Core Family for DSP Applications](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00544-2B-DSP74K-PRG-01.21.pdf) (rev 1.21)
    * [Programming the MIPS32 1004K Coherent Processing System Family](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00638-2B-1004K-PRG-01.20.pdf) (rev 1.20)
    * [MIPS32 M6200 Processor Core Family Programmers Guide](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD01093-2B-M6200SW-USG-01.00.pdf) (rev 1.00)
    * [MIPS32 M6250 Processor Core Family Programmers Guide](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS32_MD01099_M6250_ProgrammersGuide_USG_01.00.pdf) (rev 1.00)
    * [MIPS64 I6400 Multiprocessing System Programmers Guide](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS_Warrior_I6400_ProgrammerGuide_MD01196_P_1.00.pdf) (rev 1.00)
    * [MIPS64 I6500 Multiprocessing System Programmers Guide](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS_Warrior_I6500_ProgrammerGuide_MD01179_P_1.00.pdf) (rev 1.00)
* **BusBridge Module Users Manual**
    * [MIPS BusBridge 2 Module Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00429-2B-MABUG-USM-02.06.pdf) (rev 2.06)
    * [MIPS BusBridge 2 Module Errata](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00473-2B-MABERS-ERS-01.03.pdf) (rev 1.03)
    * [MIPS BusBridge 3 Module Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00660-2B-PLATFORMS-USM-02.02.pdf)  (rev 2.02)
* **Instruction Set Quick Reference**
    * [MIPS32 Instruction Set Quick Reference](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00565-2B-MIPS32-QRC-01.01.pdf)
    * [MIPS DSP ASE Instruction Set Quick Reference](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00566-2B-MIPSDSP-QRC-01.00.pdf)
* **MIPS Virtualization Documents**
    * [Hardware-assisted Virtualization with the MIPS Virtualization Module](https://s3-eu-west-1.amazonaws.com/downloads-mips/mips-documentation/login-required/hardware_assisted_virtualization_with_the_mips_virtualization_module.pdf)
    * [Using Virtualization to Implement a Scalable Trusted Execution Environment in Secure SoCs](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00993-2B-VZMIPS-WHT-01.00.pdf)
    * [MIPS Virtualization: Processor and SOC Support Overview](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD01171-2B-SOCVirtualization-01.00.pdf)
* **MIPS Multithreading Documents**
    * [An Overview of MIPS Multi-Threading White Paper](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/Overview_of_MIPS_Multi_Threading.pdf)
    * [Multi-Threading Applications on the MIPS32 34K Core](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/multi_threading_applications_on_mips32_34k.pdf)
    * [Increasing Application Throughput on the MIPS32 34K Core Family with Multithreading](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/increasing_application_throughput_on_mips32_34k_with_multithreading.pdf)
    * [Multi-Threading for Efficient Set Top Box SoC Architectures](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/multi-threading_for_efficient_set_top_box_soc_architectures.pdf)
    * [Optimizing Performance, Power, and Area in SoC Designs Using MIPS Multithreaded Processors](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/optimizing_performance_power_and_area_in_soc_designs_using_mips_multi-%20threaded_processors.pdf)
    * [Thread Switching Policies in a Multi-threaded Processor](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/thread_switching_policies_in_a_multi-threaded_processor.pdf)
    * [MIPS MT Principles of Operation](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00452-2B-MIPSMT-WHP-01.02.pdf)
* **MIPS ABI Specifications**
    * [MIPS o32 ABI Documentation](https://refspecs.linuxfoundation.org/elf/mipsabi.pdf)
    * [MIPSpro N32 ABI Handbook](https://math-atlas.sourceforge.net/devel/assembly/007-2816-005.pdf)
    * [MIPS o64 ABI for GCC](https://gcc.gnu.org/projects/mipso64-abi.html)
    * [MIPS eabi Documentation](https://sourceware.org/legacy-ml/binutils/2003-06/msg00436.html)
* **nanoMIPS Documentation**
    * [dyncall MIPS Calling Conventions](https://www.dyncall.org/docs/manual/manualse11.html#x12-78000D.7)
    * [MIPS Architecture Base: nanoMIPS32 Instruction Set Technical Reference Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/I7200/I7200+product+launch/MIPS_nanomips32_ISA_TRM_01_01_MD01247.pdf)
    * [MIPS Architecture Base: 32-bit Privileged Resource Architecture Technical Reference Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/I7200/I7200+product+launch/MIPS_nanoMIPS32_PRA_06_09_MD01251.pdf)
    * [MIPS Architecture Extension: nanoMIPS32 DSP Technical Reference Manual](https://wavecomp.ai/downloads-mips/I7200/I7200-product-launch/MIPS_nanoMIPS32_DSP_00_04_MD01249.pdf)
    * [MIPS Architecture Extension: nanoMIPS32 Multithreading Technical Reference Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/I7200/I7200+product+launch/MIPS_nanoMIPS32_MT_TRM_01_17_MD01255.pdf)
* **Miscellaneous MIPS Documents**
    * [64-bit ELF Object File Specification](https://www.infania.net/misc1/sgi_techpubs/techpubs/007-4658-001.pdf) (Draft Version 2.5)
    * [MIPS Coherence Protocol Specification](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00605-2B-CMPCOHERE-AFP-01.01.pdf) (rev 1.01)
    * [Boot-MIPS: Example Boot Code for MIPS Cores](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD0901_Boot-MIPS_Example_Boot_Code_for_MIPS-Cores.1.5.19.pdf) (rev 1.59)
    * [MIPS SIMD Architecture](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MD00926-2B-MSA-WHT-01.03.pdf) (rev 1.03)
    * [MIPS SEAD-3 Board Users Manual](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS-SEAD3-User-Manual.pdf)
    * [MIPS Debug Low-level Bring-up Guide](https://s3-eu-west-1.amazonaws.com/downloads-mips/documents/MIPS_Debug_Low-Level_Bring-Up_Guide_2.6.pdf)
    * [Toolchain notes on MIPS](https://maskray.me/blog/2023-09-04-toolchain-notes-on-mips)
    * [RFC: Adding non-PIC executable support to MIPS](https://gcc.gnu.org/legacy-ml/gcc/2008-06/msg00670.html)
    * [List of MIPS Architecture Processors](https://en.wikipedia.org/wiki/List_of_MIPS_architecture_processors) (Wikipedia)