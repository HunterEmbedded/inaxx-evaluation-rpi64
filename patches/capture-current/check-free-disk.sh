#!/bin/sh
# check free disk space and signal to app via /var/www/stopCapture
# when the minimum threshold has been reached

# loop indefinitely
while [ 1 ]
do

  # check disk usage
  freeSpace=`df | grep "root" | awk '{print $4}'`

  echo "free disk $freeSpace"
  # if no space available
  if [ $freeSpace -lt 100000 ]
  then
     # create the file to tell app to stop capture
     touch /var/www/stopCapture
  fi


  # sleep 10 seconds
  sleep 10

done


