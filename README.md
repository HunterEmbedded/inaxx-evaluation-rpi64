# inaxx-rpi64-evaluation
RPi64 build for INAxxx current measurement evaluation hat. Currently script supports RPi 3 and 4.


This package will build an SD card img for a RPi with a 64 bit filesystem and support for the [Hunter Embedded Current Measurement INAXXX Evaluation Board (v4.1.3)](http://www.hunterembedded.co.uk/inaxxx-evaluation-board-for-raspberry-pi-and-beaglebone/). This board is available on [Amazon](https://www.amazon.co.uk/INAxxx-Current-Measurement-Evaluation-Cape/dp/B09TYTXM68).

This board provides the following options for measuring current:

- INA219 with a 0.33ohm 1% shunt to measure currents in range +/- 970mA. There is no input filter on the INA219.
- INA180A2 with a 0.082ohm 1% shunt + ADS1018 ADC with FSR of 4.096V to measure currents in range 0-1024mA. The ADS1018 has an input low pass filter with a cut off frequency of 1682Hz.
- The footprint for an INA226 is also present but not populated due to component shortage.

All shunts are Panasonic ERJ14s. The schematics are also available on [github](https://github.com/HunterEmbedded/inaxxx-evaluation-schematics).



At the linux level it adds:
- an IIO driver for ADS1018.
- enables built in INA219 driver including a fix for a regression introduced in K5.4

At the application level it provides:
- an application to capture data from IIO and store it in an SQL database. This provides an example of on how to use IIO APIs in C code.
- webserver to allow start and stop of capture and downloading of sql file to host.
- host side python3 scripts to access SQL files on BBB and use plotly to display the data. The python3 script identifies the BBB from its avahi packets.

## To install: 
```
$ git clone https://github.com/HunterEmbedded/inaxx-evaluation-rpi64
```

Assumption is that git is installed and configured already.

Build script is for Ubuntu 20.04 but with instructions on how to modify docker installation for other versions. The build can take over an hour as the filesystem is created. Answer yes to each installation approval request.

## To build: 
The build script defaults to a RPi 4 build. To build for RPi3 change the value of variable PI_DEFCONFIG to bcmrpi3_defconfig.
```
PI_DEFCONFIG=bcmrpi3_defconfig
```
Run the build script.
```   
$ cd inaxx-evaluation-rpi64 
inaxx-evaluation-rpi64$ ./build-rpi-image-inaxxx.sh
```
The output of the build process is an SD card image file which contains the actual filesystem build date (eg 2022-03-21-Raspbian-INAxxx-lite.img) 

`raspbian-image/BUILD-DATE-Raspbian-INAxxx-lite.img`

## To create the SD card: 
Use Balena Etcher (https://www.balena.io/etcher/) to write the IMG file to SD card.


## To run:
Log in via SSH (address allocated by your DHCP server) or via a terminal over serial. Use the username "pi" and password "raspberry". 
There are two examples to help understand the application. These must be run using `sudo` to access the IIO sysfs.
- /opt/iio-command-line-v4.1.3.sh controls the IIO via sysfs
- /opt/iio-app-test-v4.1.3.sh calls the C app to capture using IIO and store to a local database 
    
To visualise the data from the host/development PC (assuming RPi is connected via Ethernet to the network)
```
cd python
python$ python3 show-current.py
```    
This will connect to the RPi (identified by Avahi packets), list available current capture database files and then allow one to be selected and displayed.
