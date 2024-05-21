#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.10.217
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # build the kernel
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j8 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/arm64/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# create sysroot
mkdir -p rootfs && cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

# make busybox
make distclean
make defconfig
make -j8 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/busybox/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/busybox/busybox | grep "Shared library"

# Add library dependencies to rootfs
INTERPRETER=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/busybox/busybox | grep "program interpreter" | grep -oE "/([a-Z]|/|-|[0-9]|\.)*[0-9]")
echo "Copying Interpreter: "${INTERPRETER#.*/}

#cp -v /usr/aarch64-linux-gnu/lib/${INTERPRETER#/lib/} ${OUTDIR}/rootfs/lib
find / -name "${INTERPRETER#/lib/}" -exec cp -v {} ${OUTDIR}/rootfs/lib \; -quit 2>/dev/null

SHARED_LIBS=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/busybox/busybox | grep "Shared library" | grep -oE "\[([a-Z]|-|[0-9]|\.)*\]" | sed 's/\[//' | sed 's/\]//')
echo "Copying Shared libs: "$(echo ${SHARED_LIBS} | tr '\n' ' ')
#cp -v ${SHARED_LIBS} ${OUTDIR}/rootfs/lib64
#cp -v ${SHARED_LIBS} ${OUTDIR}/rootfs/lib
for LIB in ${SHARED_LIBS}; do
	find / -path "*aarch64*${LIB}" -exec cp -v {} ${OUTDIR}/rootfs/lib \; -quit 2>/dev/null
	find / -path "*aarch64*${LIB}" -exec cp -v {} ${OUTDIR}/rootfs/lib64 \; -quit 2>/dev/null
done

# Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/console c 1 5

# Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
mkdir -p ${OUTDIR}/rootfs/home/conf
cp writer finder-test.sh autorun-qemu.sh ${OUTDIR}/rootfs/home
cp finder.sh ${OUTDIR}/rootfs/home
cp conf/username.txt conf/assignment.txt ${OUTDIR}/rootfs/home/conf

# Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs

# Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ${OUTDIR}
gzip -f initramfs.cpio
