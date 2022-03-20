#!/bin/sh   

# This script is a quick way to test that the connections 
# for each of the ina devices are correctly setup by
# dumping sampled values to the command line.


cd /sys/bus/iio/devices/iio\:device0/
#enable current capture
echo 1 > scan_elements/in_current3_en 
echo 1 > buffer/enable


cat /dev/iio:device0 | xxd &

sleep 1
echo 0 > buffer/enable

# stop the cat process that owns device1
killall cat

cd /sys/bus/iio/devices/iio\:device1/
#enable current capture
echo 1 > scan_elements/in_current3_en 
# enable collection of timestamps
echo 1 > buffer/enable

# dump captured data, note this is 16 bit little endian, so swap bytes to get 16 bit values
cat /dev/iio:device1 | xxd &
sleep 1

echo 0 > buffer/enable

# stop the cat process that owns device1
killall cat


# set up trigger for ads1018
mkdir -p /sys/kernel/config/iio/triggers/hrtimer/trigger1
echo 10 > /sys/devices/trigger1/sampling_frequency 


cd /sys/bus/iio/devices/iio\:device2/
echo trigger1 > trigger/current_trigger 
# enable the voltage1 channel to be AIN1 and set parameters
echo 1 > scan_elements/in_voltage1_en 
echo 1600 > in_voltage1_sampling_frequency 
echo 2 > in_voltage1_scale
# enable collection of timestamps
echo 1 > scan_elements/in_timestamp_en
echo 10 > buffer/length
echo 5 > buffer/watermark
echo "enable ina180 input 1"
echo 1 > buffer/enable
cat /dev/iio:device2 | xxd &
sleep 1
echo 0 > buffer/enable
echo 0 > scan_elements/in_voltage1_en 
# stop the cat process that owns device2
killall cat

# now swap channel
echo 1 > scan_elements/in_voltage0_en 
echo 3300 > in_voltage0_sampling_frequency 
echo 1 > in_voltage0_scale
echo "enable ina180 input 0"
echo 1 > buffer/enable
cat /dev/iio:device2 | xxd &
sleep 1
echo 0 > buffer/enable
echo 0 > scan_elements/in_voltage0_en 
# stop the cat process that owns device2
killall cat



