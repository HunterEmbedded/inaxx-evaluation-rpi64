#!/bin/sh

# This script builds a 64 bit version of linux kernel and then 
# adds it to a Raspbian image to create a .img file that can be written to 
# an SD card
# It has been tested on RPi3B+

# Assumption is that if you have cloned the file then git is already installed.
# git must also have been configured with 
#  git config --global user.email "you@example.com"
#  git config --global user.name "Your Name"


if [ ! -e packages-installed ]
then
    sudo apt-get update
    sudo apt-get install git bc bison flex libssl-dev make libc6-dev libncurses5-dev crossbuild-essential-arm64 u-boot-tools
    touch packages-installed
fi


if docker -v; then
    echo "docker is already installed"
else
    echo "docker is not installed. Installing NOW"
    
    # This is for Ubuntu 20.04, taken from https://www.digitalocean.com/community/tutorials/how-to-install-and-use-docker-on-ubuntu-20-04
    # Will need to vary for a different Ubuntu version
    ubuntuVersion=`cat /etc/os-release | grep UBUNTU_CODENAME | awk -F'=' '{print $2}'`
    if [ "${ubuntuVersion}" != "focal" ]; then
        echo "Not running on Ubuntu 20.04. Need to manually install docker. \n
              Follow instructions for your version eg \n
              https://www.digitalocean.com/community/tutorials/how-to-install-and-use-docker-on-ubuntu-18-04"
              exit
    fi          
    
    #taken from https://www.digitalocean.com/community/tutorials/how-to-install-and-use-docker-on-ubuntu-20-04
    sudo apt install apt-transport-https ca-certificates curl software-properties-common
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
    sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu focal stable"
    apt-cache policy docker-ce
    sudo apt install docker-ce
    
    #sudo systemctl status docker
fi    
    
if [ ! -e pi-gen-tools.installed ]; then
   sudo apt-get update
   #sudo apt-get install quilt parted realpath qemu-user-static debootstrap zerofree pxz zip dosfstools bsdtar libcap2-bin grep rsync xz-utils
   sudo apt-get install quilt qemu-user-static debootstrap zerofree libarchive-tools kpartx
   touch pi-gen-tools.installed
fi


# Set some variables to help with the build
ROOTDIR=`pwd`

# Select 64 bit arm for kernel and correct cross compiler
ARM_TYPE=arm64
CROSS_COMPILE=aarch64-linux-gnu-
ARCH=${ARM_TYPE}


# Build base raspbian image
if [ ! -e pi-gen.downloaded ]; then
   git clone https://github.com/RPi-Distro/pi-gen || exit
   cd pi-gen || exit
   git checkout -b arm64 || exit
   # commit id on 7th February 2022
   git checkout 255288909beae446a4f1eeedaa544f1a7d421e54 || exit

   # now patch image content list to add packages we need for example app and web interface
   git am ../patches/pi-gen/0001-add-sqlite3-dev-and-lighttpd-to-installed-package-li.patch || exit
   git am ../patches/pi-gen/0002-add-php-cgi-so-that-lighttpd-can-run-php-scripts.patch || exit

   cd ..
   touch pi-gen.downloaded
fi

# Set name of RASPBIAN image to be created
export RASPBIAN_NAME="Raspbian-INAxxx"
# This must be done after build

if [ ! -e pi-gen.built ]; then
    cd pi-gen


    # create config file and note date as it will be used in image file naming
    TODAY=`date +"%F"`
    echo "${TODAY}" > build-day 
    echo "IMG_NAME='${RASPBIAN_NAME}'" > config
#    echo "CLEAN=1" >> config
    # Now build only a minimal FS so skip stages 3, 4 and 5
    # instructions from github.com/RPi-Distro/pi-gen/README.md
    touch ./stage3/SKIP ./stage4/SKIP ./stage5/SKIP
    touch ./stage4/SKIP_IMAGES ./stage5/SKIP_IMAGES

    ./build-docker.sh
    cd ..
    touch pi-gen.built
fi

###########################################################################
# Now download and patch the 5.10 kernel.
###########################################################################

# download kernel if required
if [ ! -e kernel.cloned ]; then
   git clone https://github.com/raspberrypi/linux || exit
   touch kernel.cloned
fi

if [ ! -e kernel.checkedout ]; then
   cd linux
   ##!!!! This is HEAD from rpi-5.10-y on 30th September 2021 which is date script was tested !!!!
   git checkout 5ab0b197d5c070aa06a17bd649a3e6a1f83fcd66 -b rpi-5.10-y || exit
   cd ..
   touch kernel.checkedout
fi

# patch kernel if required
if [ ! -e kernel.patched ]; then
   cd linux
   git am ../patches/kernel/v4-0001-workaround-regression-in-ina2xx-introduced-by-cb4.patch || exit
   git am ../patches/kernel/0002-add-INA2xx-and-ADS1018-drivers-to-IIO.patch || exit
   git am ../patches/kernel/0003-device-tree-overlay-for-inas-initial-version.-ina219.patch || exit
   git am ../patches/kernel/0004-rpi-cape-ads1018-driver-now-8-bits-on-spi1.patch || exit
   git am ../patches/kernel/0005-add-reuired-IIO_HRTIMER_TRIGGER-so-it-exists-in-conf.patch || exit
   git am ../patches/kernel/0006-move-back-to-spi0-and-spi1-does-not-support-CPHA-mod.patch || exit
   git am ../patches/kernel/0007-update-ads1018-driver-to-8-bit-mode-and-also-overlay.patch || exit
   git am ../patches/kernel/0008-overlay-now-matches-board-v4.1.3-with-ina219-and-ina.patch || exit
   cd ..
   touch kernel.patched
fi


###########################################################################
# Now create a new img file from a standard Raspbian that can be mounted
# and then modified with the new u-boot and kernel
###########################################################################

# pick up the date that Raspian was built to allow us to pick up correct file
RASPBIAN_BUILD_DAY=`cat pi-gen/build-day`

# copy the clean img file so that we can mount and edit it
mkdir -p raspbian-image

# If the selected RASPBIAN image does not exist, exit with an error
if [ ! -e pi-gen/deploy/image_${RASPBIAN_BUILD_DAY}-${RASPBIAN_NAME}-lite.zip ]; then
  echo "RASPBIAN_NAME needs to be set correctly in this script."
  echo "pi-gen/deploy/image_${RASPBIAN_BUILD_DAY}-${RASPBIAN_NAME}-lite.zip needs to exist"
  exit
fi  

unzip pi-gen/deploy/image_${RASPBIAN_BUILD_DAY}-${RASPBIAN_NAME}-lite.zip -d raspbian-image || exit




###########################################################################
# Now build the 64 bit kernel
# and install it to the boot partition
###########################################################################
if [ ! -e kernel.built ]
then
   cd linux || exit
   # do a full clean, uncomment if required
   #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} distclean || exit

   # configure 
   KERNEL=kernel8
   make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} bcmrpi3_defconfig || exit

   #build kernel, modules and device tree files
   make -j 10 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} Image dtbs modules || exit

   cd ..
   touch kernel.built
fi


# Set paths to mount the partitions
BOOT_PART=mnt/boot
ROOTFS_PART=mnt/rootfs
# create mount directories for the two partitions
mkdir -p ${BOOT_PART}
mkdir -p ${ROOTFS_PART}


# Assume this is standard img with following characteristics found by 
# fdisk --bytes -lo Id,Start,Size pi-gen/work/Raspbian-INAxxx/export-image/2021-09-30-Raspbian-INAxxx-lite.img
# Disk pi-gen/work/Raspbian-INAxxx/export-image/2021-09-30-Raspbian-INAxxx-lite.img: 1.88 GiB, 1991782912 bytes, 3890201 sectors
# Units: sectors of 1 * 512 = 512 bytes
# Sector size (logical/physical): 512 bytes / 512 bytes
# I/O size (minimum/optimal): 512 bytes / 512 bytes
# Disklabel type: dos
# Disk identifier: 0xea0f37ab
# 
# Id  Start       Size
#  c   8192  262144000
# 83 520192 1725444608

# Get the partition ID
PARTITION_ID=`fdisk --bytes -lo Id,Start,Size raspbian-image/${RASPBIAN_BUILD_DAY}-${RASPBIAN_NAME}-lite.img | grep 'Disk identifier' \
              | awk -F " " '{print $3}' | cut -c3-`

# parse the fdisk info to dynamically work out the values for the mount processs
BOOT_PARTITION_INFO=`fdisk --bytes -lo Id,Start,Size raspbian-image/${RASPBIAN_BUILD_DAY}-${RASPBIAN_NAME}-lite.img | grep ' c' `
ROOTFS_PARTITION_INFO=`fdisk --bytes -lo Id,Start,Size raspbian-image/${RASPBIAN_BUILD_DAY}-${RASPBIAN_NAME}-lite.img | grep '83 ' `

BOOT_PARTITION_START=`echo ${BOOT_PARTITION_INFO} | awk -F " " '{print $2}'`
BOOT_PARTITION_SIZE=`echo ${BOOT_PARTITION_INFO} | awk -F " " '{print $3}'`

# now calculate offset in bytes
BOOT_PARTITION_OFFSET=$((${BOOT_PARTITION_START}*512))


ROOTFS_PARTITION_START=`echo ${ROOTFS_PARTITION_INFO} | awk -F " " '{print $2}'`
ROOTFS_PARTITION_SIZE=`echo ${ROOTFS_PARTITION_INFO} | awk -F " " '{print $3}'`

# now calculate offset in bytes
ROOTFS_PARTITION_OFFSET=$((${ROOTFS_PARTITION_START}*512))

# This is how the offsets are calculated in the two mount instructions 
# 4194304 = 8192*512
sudo mount -v -o offset=${BOOT_PARTITION_OFFSET},sizelimit=${BOOT_PARTITION_SIZE} -t vfat raspbian-image/${RASPBIAN_BUILD_DAY}-${RASPBIAN_NAME}-lite.img ${BOOT_PART} || exit
# 266338304 = 520192*512
# sizelimit=1736757760 for second partition 
sudo mount -v -o offset=${ROOTFS_PARTITION_OFFSET},sizelimit=${ROOTFS_PARTITION_SIZE} -t ext4 raspbian-image/${RASPBIAN_BUILD_DAY}-${RASPBIAN_NAME}-lite.img ${ROOTFS_PART} || exit


cd linux
# Install kernel modules
# need to pass all defines as sudo uses a different environment
sudo make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} INSTALL_MOD_PATH=../${ROOTFS_PART} modules_install

# remove all raspbian kernel*.img files from the boot partition 
# so that one we create is guaranteed to be the one run by RPi bootloader firmware
sudo rm ../${BOOT_PART}/kerne*.img

# install linux and device tree to boot partition
sudo cp arch/${ARM_TYPE}/boot/Image ../${BOOT_PART}/Image || exit
sudo cp arch/${ARM_TYPE}/boot/dts/broadcom/*.dtb ../${BOOT_PART}/ || exit
sudo cp arch/${ARM_TYPE}/boot/dts/overlays/*.dtbo ../${BOOT_PART}/overlays || exit
sudo cp arch/${ARM_TYPE}/boot/dts/overlays/README ../${BOOT_PART}/overlays || exit

# install Image to the boot partition
# calling Image kernel8.img means that it is the executable the GPU fw will load and 8 means it is 64 bit
sudo cp arch/${ARM_TYPE}/boot/Image ../${BOOT_PART}/kernel8.img || exit
cd ..


###########################################################################
# Now configure the RPi firmware (ie GPU) to correctly run u-boot and then kernel
###########################################################################


# set up uboot script to boot the kernel using a boot.scr file in boot partition
# load kernel to 0x01080000 as that is its run address and it saves relocation
sh -c "echo '
setenv kernel_addr_r 0x01080000
fatload mmc 0:1 \${kernel_addr_r} Image
fatload mmc 0:1 0x01000000 bcm2710-rpi-3-b-plus.dtb
booti \${kernel_addr_r} - 0x01000000' > rpi3-bootscript.txt"

mkimage -A arm64 -O linux -T script -d rpi3-bootscript.txt boot.scr 
sudo cp boot.scr mnt/boot/


# configure the RPi firmware (ie GPU) using the config.txt file
# enable the UART for terminal output
#sudo sh -c "echo 'enable_uart=1' >> mnt/boot/config.txt"

# enable 64 bit arm support
sudo sh -c "echo 'arm_64bit=1' >> mnt/boot/config.txt"

# disable BT so terminal is on GPIO 13/14
sudo sh -c "echo 'dtoverlay=disable-bt' >> mnt/boot/config.txt"

# enable INA219/INA226 on I2C and ADS1018+INA180 on SPI
sudo sed -i 's/#dtparam=i2c_arm=on/dtparam=i2c_arm=on/' mnt/boot/config.txt
sudo sed -i 's/#dtparam=spi=on/dtparam=spi=on/' mnt/boot/config.txt

sudo sh -c "echo 'dtoverlay=ina-evaluation-board' >> mnt/boot/config.txt"

#create linux boot command line
sudo sh -c "echo 'console=serial0,115200 console=tty1 root=PARTUUID=${PARTITION_ID}-02 rootfstype=ext4 fsck.repair=yes rootwait' > mnt/boot/cmdline.txt"



# configure the filesystem to handle fact Bluetooth is not going to work as
# UART1 is used for terminal
# stop the hciuart service from running in systemd and trying to use UART1
sudo rm mnt/rootfs/etc/systemd/system/multi-user.target.wants/hciuart.service

# enable ssh in Linux by creating file ssh on /boot
sudo touch mnt/boot/ssh



###########################################################################
# Build application to capture IIO current samples
###########################################################################
# copy in the app
cp -r patches/capture-current . || exit
cp -r patches/python . || exit

cd capture-current || exit
make CC=${CROSS_COMPILE}gcc ROOTFSDIR=${ROOTDIR}/mnt/rootfs || exit
# create install directory in case it is not already present
sudo mkdir -p ../mnt/rootfs/opt

# and directory for sql database
sudo mkdir -p ../mnt/rootfs/var/www/sql

sudo make CC=${CROSS_COMPILE}gcc TARGETDIR=${ROOTDIR}/mnt/rootfs/opt install  || exit
sudo mkdir -p ${ROOTDIR}/mnt/rootfs/etc/avahi/services  || exit
sudo cp cm.service ${ROOTDIR}/mnt/rootfs/etc/avahi/services  || exit
sudo mkdir -p ${ROOTDIR}/mnt/rootfs/etc/lighttpd
sudo cp lighttpd-current-capture.conf ${ROOTDIR}/mnt/rootfs/etc/lighttpd/lighttpd.conf  || exit

sudo cp test.sh ${ROOTDIR}/mnt/rootfs/opt/  || exit
sudo cp iio-command-line-v4-1-3.sh ${ROOTDIR}/mnt/rootfs/opt/  || exit
sudo cp iio-app-test-v4-1-3.sh ${ROOTDIR}/mnt/rootfs/opt/  || exit


# and index page and conf files for run script
sudo mkdir -p  ${ROOTDIR}/mnt/rootfs/var/www
sudo cp -r www/*  ${ROOTDIR}/mnt/rootfs/var/www/
sudo cp -r php/*  ${ROOTDIR}/mnt/rootfs/var/www/


cd ..

sudo install -D -m 775 patches/capture-current/iio-command-line-v4-1-3.sh ${ROOTDIR}/mnt/rootfs/opt
sudo install -D -m 775 patches/capture-current/iio-app-test-v4-1-3.sh ${ROOTDIR}/mnt/rootfs/opt

###########################################################################
# Finally unmount the updated partitions
###########################################################################

sudo umount ${BOOT_PART} || exit
sudo umount ${ROOTFS_PART} || exit

echo "partitions unmounted"

###########################################################################
# The file raspbian-image/uboot-linux64.img is now ready to be written to 
# an SD card with a tool like Etcher
###########################################################################

