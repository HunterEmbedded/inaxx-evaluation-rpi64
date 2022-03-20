#!/bin/sh

# script to test the foure current capture options on the INA evaluation cape




# remove stop file if it exists
if [ -e /var/www/stopCapture ]
then
    rm /var/www/stopCapture
fi


# INA219
./capture-current-iio --device=ina219 --element=current3 --ina2xx_Imax=970 --ina2xx_tbus_us=8510 --ina2xx_tshunt_us=8510  &

# sleep for 10 seconds then stop app by creating a file
sleep 10
touch /var/www/stopCapture
sleep 1
rm /var/www/stopCapture

# INA226
./capture-current-iio --device=ina226 --element=current3 --ina2xx_Imax=970 --ina2xx_tbus_us=140 --ina2xx_tshunt_us=140 --ina226_avg=1 &

# sleep for 10 seconds then stop app by creating a file
sleep 1
touch /var/www/stopCapture
sleep 1
rm /var/www/stopCapture

# INA180 on ADS1018 channel0
#./capture-current-iio --device=ads1018 --element=voltage0 --trigger --ads1018_fsr=2048 --ina180_gain=20 --ina180_shunt=1500 &
./capture-current-iio --device=ads1018 --element=voltage0 --trigger --ads1018-sample-rate=128 --ads1018_fsr=2048 --ina180_gain=20 --ina180_shunt=1500 &

# sleep for 10 seconds then stop app by creating a file
sleep 1
touch /var/www/stopCapture
sleep 1
rm /var/www/stopCapture

# INA180 on ADS1018 channel1
./capture-current-iio --device=ads1018 --element=voltage1 --trigger --ads1018-sample-rate=128 --ads1018_fsr=2048 --ina180_gain=20 --ina180_shunt=150 &

# sleep for 10 seconds then stop app by creating a file
sleep 1
touch /var/www/stopCapture
sleep 1
rm /var/www/stopCapture
