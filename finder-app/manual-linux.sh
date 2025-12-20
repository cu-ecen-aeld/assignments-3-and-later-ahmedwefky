#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

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

    # Generate the defconfig file for the kernel
    echo "Generating the Linux kernel config for ${KERNEL_VERSION} for ${ARCH} architecture"
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    # Build the kernel image
    echo "Building the linux kernel ${KERNEL_VERSION} for ${ARCH} architecture"
    make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image
fi

cd "$OUTDIR"
# Remove the existing rootfs directory if it exists
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Adding the kernel image to the outdir
echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

# Create rootfs directory
echo "Creating the staging directory for the root filesystem"
mkdir -p ${OUTDIR}/rootfs

cd "$OUTDIR"
# Clone busybox if it doesn't exist
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
else
    cd busybox
    make distclean
fi

# Generate the default config for busybox
make defconfig
# Build BusyBox
make -j$(nproc) CROSS_COMPILE=${CROSS_COMPILE}
# Install BusyBox into the target rootfs
make CONFIG_PREFIX=${OUTDIR}/rootfs CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
# Get program interpreter
interpeter=$(${CROSS_COMPILE}readelf -a busybox | grep "program interpreter" | sed -n 's/.*lib\///p' | sed -n 's/]//p')

# Add library dependencies for busybox into rootfs
# Copy the program interpreter to the rootfs
mkdir -p "${OUTDIR}/rootfs/lib"
cp "${SYSROOT}/lib/${interpeter}" "${OUTDIR}/rootfs/lib/${interpeter}"

# Get a list of busybox shared libraries
libs=$(${CROSS_COMPILE}readelf -a busybox | grep "Shared library" | sed -n 's/.*\[//p' | sed -n 's/]//p')
mkdir -p "${OUTDIR}/rootfs/lib64"
# Copy each library to the rootfs
for lib in ${libs}; do
  cp "${SYSROOT}/lib64/${lib}" "${OUTDIR}/rootfs/lib64/${lib}"
done

# Make device nodes
mkdir -p "${OUTDIR}/rootfs/dev"
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/console c 5 1

# Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make CROSS_COMPILE=${CROSS_COMPILE} all

# Copy the finder related scripts and executables to the home directory on the target rootfs
mkdir -p "${OUTDIR}/rootfs/home"
cp -r ${FINDER_APP_DIR}/* ${OUTDIR}/rootfs/home
mkdir -p "${OUTDIR}/rootfs/conf"
cp ${FINDER_APP_DIR}/../conf/* ${OUTDIR}/rootfs/conf

# Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs

# Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -o | gzip -f > ${OUTDIR}/initramfs.cpio.gz