#!/bin/bash   

# This script is a quick way to test that the connections 
# for each of the ina devices are correctly setup by
# dumping sampled values to the command line.

# detect which ina is on which iio device
ina=`cat /sys/bus/iio/devices/iio\:device0/name`
if [[ "$ina" == "ina219" ]]; then
    ina219device="device0"
    ads1018device="device1"
else
    ina219device="device1"
    ads1018device="device0"
fi




cd /sys/bus/iio/devices/iio\:${ina219device}/ || exit

#enable current capture
echo 1 > scan_elements/in_timestamp_en
# write vbus and vshunt integration times
echo 0.0681 > in_voltage0_integration_time
echo 0.0681 > in_voltage1_integration_time
echo 1 > scan_elements/in_current3_en 
echo 1 > buffer/enable


cat /dev/iio:${ina219device} | xxd &

sleep 1
echo 0 > buffer/enable

# stop the cat process that owns device
killall cat

# set up trigger for ads1018
mkdir -p /sys/kernel/config/iio/triggers/hrtimer/trigger1
echo 3300 > /sys/devices/trigger1/sampling_frequency 

cd /sys/bus/iio/devices/iio\:${ads1018device}/
echo trigger1 > trigger/current_trigger 
# enable the voltage0 channel to be AIN0 and set parameters
echo 1 > scan_elements/in_voltage0_en
echo 128 > in_voltage0_sampling_frequency
# scale is specified as a factor of the default of 2.048V
echo 2 > in_voltage0_scale
# enable collection of timestamps
echo 1 > scan_elements/in_timestamp_en
echo 10 > buffer/length
echo 5 > buffer/watermark
echo "enable ina180 input 0"
echo 1 > buffer/enable
cat /dev/iio:${ads1018device} | xxd &
sleep 1
echo 0 > buffer/enable
echo 0 > scan_elements/in_voltage0_en 
# stop the cat process that owns device2
killall cat


