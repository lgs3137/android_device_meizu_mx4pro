#!/system/bin/sh
/system/xbin/daemonsu --auto-daemon &
stop
setenforce 0
echo high > /sys/power/power_mode
unset LD_PRELOAD
cat /dev/input/event6 > /dev/keycheck & sleep 1
kill -9 $!
if [ -s /dev/keycheck -o -e /cache/recovery/command ];then
echo 50 > /sys/class/timed_output/vibrator/enable
mount -o remount,rw /
mount -o remount,rw /system

SVCRUNNING=$(getprop | grep -E '^\[init\.svc\..*\]: \[running\]')

rm -rf /cache/recovery/command
rm -f /etc
cp -Rf /system/twrp/* /
chmod -R 755 /sbin
export PATH=/sbin:/system/bin
export LD_LIBRARY_PATH=.:/sbin
mkdir -p /boot
mkdir -p /recovery
mkdir -p /sideload
rm -f /sdcard
mkdir -p /sdcard
mkdir -p /tmp
mount -t tmpfs tmpfs /tmp
chown 0.1000 /tmp
chmod 0775 /tmp

for SVC in ${SVCRUNNING}; do
  SVCNAME=$(expr ${SVC} : '\[init\.svc\.\(.*\)\]:.*')
  [ "${SVCNAME}" != "flash_recovery" ] && [ "${SVCNAME}" != "ueventd" ] && [ "${SVCNAME}" != "healthd" ] && stop ${SVCNAME}
done

killall -9 daemonsu

umount /mnt/shell/emulated
umount -d /mnt/cdrom

export ANDROID_ROOT=/system
export ANDROID_DATA=/data
export EXTERNAL_STORAGE=/sdcard
umount -l /system
mount /dev/block/by-name/system /system
/sbin/recovery &
else
start
fi
rm -f /dev/keycheck
