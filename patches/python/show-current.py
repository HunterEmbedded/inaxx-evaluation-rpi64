#!/usr/bin/python

# Setup instructions
# install plotly for python - tested on Ubuntu 20.04
# sudo apt-get install python3-dev libsqlite3-dev python3-pip
# sudo pip3 install zeroconf
# sudo pip3 install pysqlite3
# sudo pip3 install plotly


# 
import sqlite3
import plotly
import plotly.graph_objs as go
from datetime import date,timedelta,datetime
import urllib.request
import array as arr
import binascii
import struct
import time
import socket
import sys
from time import sleep
from typing import cast



from zeroconf import ServiceBrowser, Zeroconf

serverIP=''


class MyListener:

    def remove_service(self, zeroconf, type, name):
        print("Service %s removed" % (name,))

    def add_service(self, zeroconf, type, name):
        global serverIP
        info = zeroconf.get_service_info(type, name)
        serverIP=info.parsed_addresses()[0]
        
    def update_service(self, zeroconf, type, name):
        pass        


zeroconf = Zeroconf()
listener = MyListener()

browser = ServiceBrowser(zeroconf, "_cm._tcp.local.", listener)
try:
    while serverIP == '':
          sleep(1)
finally:
    zeroconf.close()




# Now we have the IP address, see what data files are there
# copy file from http server and store locally with same name 
httpReadDataFiles="http://%s/%s" % (serverIP,"list-data-files.php")
dataFileList="listOfDataFiles"
print(httpReadDataFiles)

urllib.request.urlretrieve(httpReadDataFiles,dataFileList)


f=open(dataFileList,"r")
fileList=f.readlines()

print("fileList %s" % (fileList))
# list always has the last element as "\n" at this point
# so remove the last element with a .pop
fileList.pop(len(fileList)-1)

print("Available Data files\n")
for i,name in enumerate(fileList):
      print(i,name)

print("Select file number to show\n");

fileNo=int(sys.stdin.readline())

# validate entered file number
if ((fileNo < 0) or (fileNo >= len(fileList))):
      print("Invalid file number entered. Exiting\n")
      sys.exit()

# Set file name
dayFileName=fileList[fileNo]
print("dayFileName %s" % (dayFileName))

# remove the \n from the end of filename
strippedDayFileName=dayFileName[:-1]
print("strippedDayFileName 1 %s" % (strippedDayFileName))
# and leading sys/ directory
strippedDayFileName=strippedDayFileName.lstrip('sql/')
print("strippedDayFileName 2 %s" % (strippedDayFileName))


# Set the layout with title to match the UUID
pTitle='Current'
pYTitle='mA'



# Now populate actual structure with correct text
plotLayout = dict(title = pTitle,
              xaxis = dict(title = 'Time', type ='date'),
              yaxis = dict(title = pYTitle, rangemode='tozero')
              )


plotData=[];


mAValues=[];
sampleTimes=[];


# copy file from http server and store locally with same name 
httpString="http://%s/sql/%s" % (serverIP,strippedDayFileName) 
print("http %s fileName %s" % (httpString,strippedDayFileName))
urllib.request.urlretrieve(httpString,strippedDayFileName)

# Connecting to the database file
conn = sqlite3.connect(strippedDayFileName)
c = conn.cursor()


# extract a line from the database
c.execute("SELECT * FROM currentData")
ADCValuesArray=arr.array('h')

lastTime=0

for row in c.execute("SELECT * FROM currentData"):
    startTime=row[0]
    scalingFactor=row[1]
    sampleRate=row[2]
    numberofSamplesPerBlob=row[3]
    ADCValuesArray=row[4]
    timeDeltasArray=row[5]

    # create a list to hold the values read from packed buffer "blob"
    mAValuesBlock = [0]*numberofSamplesPerBlob
    sampleTimesBlock = [0]*numberofSamplesPerBlob

    
    sampleTime=startTime;

    for i in range(numberofSamplesPerBlob): 
	# as we are reading shorts offset is index * 2 bytes, convert nA to mA
        mAValuesBlock[i]= (struct.unpack_from('=h',ADCValuesArray,i*2)[0] * scalingFactor)/(1000*1000)
        sampleTime += (struct.unpack_from('=h',timeDeltasArray,i*2)[0])
        sampleTimesBlock[i]=sampleTime/1000.0;
        
    lastTime=startTime
    mAValues.extend(mAValuesBlock)
    sampleTimes.extend(sampleTimesBlock)
    

conn.close() 


# Add the data extracted to a plot
plotData.append(go.Scatter( y=mAValues,x=sampleTimes,mode='lines',name="DuT"))


# And finally display the data
plotly.offline.plot({"data": plotData, "layout": plotLayout})


