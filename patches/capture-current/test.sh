#!/bin/bash

# remove "stop" file if it exists
if [ -e /var/www/stopCapture ]
then
   rm /var/www/stopCapture
fi   


#start capture
/opt/capture-current-iio &


sleep 10

# then stop it
touch /var/www/stopCapture
echo "touch"
