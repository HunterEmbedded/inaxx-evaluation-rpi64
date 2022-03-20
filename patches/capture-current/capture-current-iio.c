/* Industrialio buffer test code.
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is primarily intended as an example application.
 * Reads the current buffer setup from sysfs and starts a short capture
 * from the specified device, pretty printing the result after appropriate
 * conversion.
 *
 * Command line parameters
 * generic_buffer -n <device_name> -t <trigger_name>
 * If trigger name is not specified the program assumes you want a dataready
 * trigger associated with the device and goes looking for it.
 *
 */

/* Required for asprintf()  */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include <linux/types.h>
#include <string.h>
#include <poll.h>
#include <endian.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include "iio_utils.h"
#include "sql-manager.h"


#define SAMPLE_FREQ_HZ 		2400
#define SAMPLE_PERIOD_US	(1000000/SAMPLE_FREQ_HZ)

t_logData currentSamples;

int find_type_by_name(const char *name, const char *type);



typedef enum {
	ads1018=0,
	ina219,
	ina226,
	none
}tDevice;

char defaultDevice[]="ads1018";
char *pActualDeviceStr=NULL;
tDevice actualDeviceEnum=none;

char defaultElement[]="voltage0";
char *pActualElement=NULL;

int actualDeviceChannel;
int actualSampleRate=SAMPLE_FREQ_HZ;
bool actualTrigger=false;


/* As the ADS1018 just measures voltage we must also pass it the INA180 details 
 * so that we can calculate the current.
 * INA2xx driver reports current so calculation unnecessary
 */ 
int ads1018_fsr = 2048;
int ina180_gain = 20;
int shunt_mOhm = 150; 



/* INA2xx sample rate is determined by total period for a sample. This is */
/* (Vbus_sample_time + Vshunt_sample_time) * samples_to_average           */
/* Set the default values so that they can be modified                    */
#define INA226_DEFAULT_SAMPLE_TIME_US		1100
#define INA219_DEFAULT_SAMPLE_TIME_US		532
#define INA226_DEFAULT_SAMPLE_AVERAGE		4
int ina2xx_Vshunt_sample_time_us;
int ina2xx_Vbus_sample_time_us;
int ina226_samples_to_average = INA226_DEFAULT_SAMPLE_AVERAGE;


/* For INA2xx need to know max current to be measured to work out optimal */
/* calibration factor based on current and shunt resistance               */
int ina2xx_maxCurrent_mA = 970;

/* Factor to convert ADC output to mA, it is in nA per LSB */
int scalingFactorLSB;

void checkBeforeFree(void * buf)
{
	if (buf)
		free(buf);
	else
		printf("NULL buffer\n");    
}

/**
 * size_from_channelarray() - calculate the storage size of a scan
 * @channels:		the channel info array
 * @num_channels:	number of channels
 *
 * Has the side effect of filling the channels[i].location values used
 * in processing the buffer output.
 **/
int size_from_channelarray(struct iio_channel_info *channels, int num_channels)
{
	int bytes = 0;
	int i = 0;

	while (i < num_channels) {
		if (bytes % channels[i].bytes == 0)
			channels[i].location = bytes;
		else
			channels[i].location = bytes - bytes % channels[i].bytes
					       + channels[i].bytes;

		bytes = channels[i].location + channels[i].bytes;
		i++;
	}

	return bytes;
}

int16_t get2byte(uint16_t input, struct iio_channel_info *info)
{
	/*
	 * Shift before conversion to avoid sign extension
	 * of left aligned data
	 */
	input >>= info->shift;
	input &= info->mask;
	return ((int16_t)(input << (16 - info->bits_used)) >>
			      (16 - info->bits_used));
}

int64_t getTimestampUs(uint64_t input, struct iio_channel_info *info)
{
	/*
	 * Shift before conversion to avoid sign extension
	 * of left aligned data
	 */
	input >>= info->shift;
	input &= info->mask;
	/* Convert to microseconds from nanoseconds before returning */
	return((int64_t)(input << (64 - info->bits_used)) >>
			      (64 - info->bits_used))/1000;
}


char *dev_dir_name = NULL;
char *buf_dir_name = NULL;
bool current_trigger_set = false;

void cleanup(void)
{
	int ret;
	printf("cleanup()\n");
	fflush(stdout);
	
	/* Disable buffer */
	if (buf_dir_name) {
		ret = write_sysfs_int("enable", buf_dir_name, 0);
		if (ret < 0)
			fprintf(stderr, "Failed to disable buffer: %s\n",
				strerror(-ret));
	}

	fflush(stdout);

	/* Disable trigger */
	if (dev_dir_name && current_trigger_set) {
		/* Disconnect the trigger - just write a dummy name. */
		ret = write_sysfs_string("trigger/current_trigger",
					 dev_dir_name, "NULL");
		if (ret < 0)
			fprintf(stderr, "Failed to disable trigger: %s\n",
				strerror(-ret));
		current_trigger_set = false;
	}
	fflush(stdout);


	/* Checkpoint data base to clean up -wal and -shm files */
	printf("call closeSqliteDB()\n"); 
	fflush(stdout);
	closeSqliteDB();
        
	fflush(stdout);

}

void sig_handler(int signum)
{
	fprintf(stderr, "Caught signal %d\n", signum);
	printf("stdout Caught signal %d\n", signum);
	cleanup();
	exit(-signum);
}

void register_cleanup(void)
{
	struct sigaction sa = { .sa_handler = sig_handler };
	const int signums[] = { SIGINT, SIGTERM, SIGABRT };
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(signums); ++i) {
		ret = sigaction(signums[i], &sa, NULL);
		if (ret) {
			perror("Failed to register signal handler");
			exit(-1);
		}
	}
}

int main(int argc, char **argv)
{
	unsigned long buf_len;
	int buf_watermark;

	int ret, i, toread;
	int fp = -1;
	struct stat st = {0};

	int num_channels = 1;
	char *trigger_name = NULL, *device_name = NULL;

	char *data = NULL;
	char *element_name = NULL;
	ssize_t read_size;
	int dev_num = -1, trig_num = -1;
	char *buffer_access = NULL;
	int scan_size;
	char *trig_dev_name;

	char sqlDatabaseName[64];
	time_t nowSinceEpoch;
	struct tm *pNowDate;
	int currentDay;

	bool stopFileDoesNotExist;
	int stringLength;
	float scalingFactorLSBFloat;   


	struct iio_channel_info *channels = NULL;

	register_cleanup();

	// Set up the current cape parameters
	//dev_num = 2;     // iio:device2
	trig_num = 1;    // trigger1 
	buf_len = NUMBER_SAMPLES_PER_DB_WRITE*2;    	// length of buffer
	buf_watermark = NUMBER_SAMPLES_PER_DB_WRITE;    // watermark in buffer
	
	
	while (1) {
		int c;

        
		static struct option long_options[]={   
			{"device"          , required_argument,    0   ,'d'},
			{"element"         , required_argument,    0   ,'e'},
			{"ads1018-sample-rate"     , required_argument,    0   ,'s'},
			{"trigger"         , no_argument,          0   ,'t'},
			{"ads1018_fsr"     , required_argument,    0   ,'v'},
			{"ina180_gain"     , required_argument,    0   ,'g'},
			{"ina180_shunt"    , required_argument,    0   ,'r'},
			/* numbers just used here as obvious letters are already used */
			{"ina2xx_tbus_us"  , required_argument,    0   ,'1'},
			{"ina2xx_tshunt_us", required_argument,    0   ,'2'},
			{"ina226_avg"      , required_argument,    0   ,'3'},
			{"ina2xx_Imax"     , required_argument,    0   ,'m'},
			{0,0,0,0}
		};
        
	int option_index = 0;
        
	c = getopt_long(argc,argv,"d:e:s:tv:g:r:1:2:3:m:", long_options, &option_index);
        
        if (c == -1)
            break;
        
        
		switch(c) {
		    
			case 'd':
			{       stringLength = strlen(optarg);
				pActualDeviceStr = malloc(stringLength+1);
				strcpy(pActualDeviceStr,optarg);

				// Now we have the string, select the appropriate enum value as that
				// will be used for device choices
				if (!strcmp("ads1018",pActualDeviceStr))
				{
					actualDeviceEnum=ads1018;
				}
				else if (!strcmp("ina219",pActualDeviceStr))
				{
					actualDeviceEnum=ina219;
					ina2xx_Vshunt_sample_time_us=INA219_DEFAULT_SAMPLE_TIME_US;
					ina2xx_Vbus_sample_time_us=INA219_DEFAULT_SAMPLE_TIME_US;
				}
				else if (!strcmp("ina226",pActualDeviceStr))
				{
					actualDeviceEnum=ina226;
					ina2xx_Vshunt_sample_time_us=INA226_DEFAULT_SAMPLE_TIME_US;
					ina2xx_Vbus_sample_time_us=INA226_DEFAULT_SAMPLE_TIME_US;
				}
				else 
					{
					fprintf(stderr, "Unknown device selected %s. Options are ads1018, ina219, ina226\n",pActualDeviceStr);
					return -1;
					}

		            break;
			}


			case 'e':
			{       stringLength = strlen(optarg);
				pActualElement = malloc(stringLength+1);
				strcpy(pActualElement,optarg);
				break;
			}
			case 's':
			{       actualSampleRate = atoi(optarg);
				
				if (actualDeviceEnum!=ads1018)
				{
					fprintf(stderr, "sample rate option only valid with device ads1018\n");
					return -1;
				}			
			
		            break;
			}
			case 't':
			{       actualTrigger = true;
				
				if (actualDeviceEnum!=ads1018)
				{
					fprintf(stderr, "trigger option only valid with device ads1018\n");
					return -1;
				}			
			
		            break;
			}
			case 'v':
			{       ads1018_fsr = atoi(optarg);
				if (actualDeviceEnum!=ads1018)
				{
					fprintf(stderr, "ads1018_fsr option only valid with device ads1018\n");
					return -1;
				}			
				break;
			}
			case 'g':
			{       ina180_gain = atoi(optarg);
				if (actualDeviceEnum!=ads1018)
				{
					fprintf(stderr, "ina180_gain option only valid with device ads1018\n");
					return -1;
				}			
				break;
			}
			case 'r':
			{       shunt_mOhm = atoi(optarg);
				break;
			}
			case '1':
			{ 
				if ((actualDeviceEnum == ina219) || (actualDeviceEnum == ina226))
					ina2xx_Vbus_sample_time_us = atoi(optarg);
				else
				{
					fprintf(stderr, "ina226_tbus_us option only valid with device ina226\n");
					return -1;
				}	
				break;
			}
			case '2':
			{       
				if ((actualDeviceEnum == ina219) || (actualDeviceEnum == ina226))
					ina2xx_Vshunt_sample_time_us = atoi(optarg);
				else
				{
					fprintf(stderr, "ina2xx_tshunt_us option only valid with devices ina219 and ina226\n");
					return -1;
				}			
				break;


				break;
			}
			case '3':
			{       
				if (actualDeviceEnum == ina226)
					ina226_samples_to_average = atoi(optarg);
				else
				{
					fprintf(stderr, "ina226_avg option only valid with devices ina226\n");
					return -1;
				}	
				break;
			}
			case 'm':
			{       
				if ((actualDeviceEnum == ina226) || (actualDeviceEnum == ina219))
					ina2xx_maxCurrent_mA = atoi(optarg);
				else
				{
					fprintf(stderr, "ina2xx_avg option only valid with devices ina219 and ina226\n");
					return -1;
				}	
				break;
			}
		}
	}
	// Set the default device name if not set by arguments
	if (pActualDeviceStr == NULL)
	{
		stringLength = strlen(defaultDevice);
		pActualDeviceStr = malloc(stringLength+1);
		strcpy(pActualDeviceStr,defaultDevice);
		actualDeviceEnum = ads1018;
	}

	// Set the default device name if not set by arguments
	if (pActualElement == NULL)
	{
		stringLength = strlen(defaultElement);
		pActualElement = malloc(stringLength+1);
		strcpy(pActualElement,defaultElement);
	}
    
    
	/* Work out scaling factor based on device */
	if (actualDeviceEnum == ads1018)
	{
		// It's an ads1018 so need to work out scaling factor (nA per LSB)
		// 2048 is because it is an 12 bit signed ADC (can't use negative range in single ended
		// and so only 11 bits are valid
		printf("fsr %d shunt %d gain %d\n",ads1018_fsr,shunt_mOhm,ina180_gain);
		scalingFactorLSBFloat = (((float)ads1018_fsr*1000.0*1000.0*1000.0)/(2048.0*(float)shunt_mOhm*(float)ina180_gain));
		printf("scaling float %f\n",scalingFactorLSBFloat);
		scalingFactorLSB=(int)scalingFactorLSBFloat;
	}
	else
	{
		// It's an ina2xx so scaling factor is based on max current measured over 15 bits.
		// It is current per LSB and is in nA with the multiplication by 1000 to allow it to be an int
		scalingFactorLSBFloat = ((float)ina2xx_maxCurrent_mA*1000.0*1000.0)/32768.0;
		scalingFactorLSB=(int)scalingFactorLSBFloat;
	}
    
	printf("Scaling factor (nA per LSB ) %d\n", scalingFactorLSB);
    
	// Now find device number in IIO based on its name
	dev_num = find_type_by_name(pActualDeviceStr, "iio:device");

	/* Create name for SQL db based on current time */
	nowSinceEpoch = time(NULL);
	pNowDate=gmtime(&nowSinceEpoch);
	if (pNowDate)
	{
		sprintf(sqlDatabaseName,"/var/www/sql/currentCapture-%02d-%02d-%02d-%02d:%02d:%02d",pNowDate->tm_mday, pNowDate->tm_mon+1, 
				pNowDate->tm_year+1900,pNowDate->tm_hour, pNowDate->tm_min, pNowDate->tm_sec);
		printf("File name %s\n",sqlDatabaseName);
            
		// Set the current day so we can use to check for new day
		currentDay=pNowDate->tm_mday;
            
	}
	else
	{
		printf("ERROR: Unable to create SQL database name\n");    
		return -1;
	}
           
	if (openSqliteDB(sqlDatabaseName) == 1)
	{
		printf("ERROR: Unable to open SQL database\n");    
		return -1;
	}
           
	/* A trigger is used for ADC + INA180 configurations */
	if (actualTrigger)
	{
		/* Create the hr trigger for the sampling */
		if (stat("/sys/kernel/config/iio/triggers/hrtimer/trigger1", &st) == -1){ 
			mkdir("/sys/kernel/config/iio/triggers/hrtimer/trigger1",0700);
		}

		/* set the sampling rate for the trigger*/
		ret = write_sysfs_int("sampling_frequency", "/sys/devices/trigger1", actualSampleRate);
		if (ret < 0) {
			fprintf(stderr, "Failed to write sampling rate\n");
			goto error;
		}
	}

	ret = asprintf(&dev_dir_name, "%siio:device%d", iio_dir, dev_num);
	if (ret < 0)
		return -ENOMEM;

	/* Fetch device_name if specified by number */
	device_name = malloc(IIO_MAX_NAME_LENGTH);
	if (!device_name) {
		ret = -ENOMEM;
		goto error;
	}
	ret = read_sysfs_string("name", dev_dir_name, device_name);
	if (ret < 0) {
		fprintf(stderr, "Failed to read name of device %d\n", dev_num);
		goto error;
	}
    
	if (actualTrigger)
	{
		ret = asprintf(&trig_dev_name, "%strigger%d", iio_dir, trig_num);
		if (ret < 0) {
			return -ENOMEM;
		}
		trigger_name = malloc(IIO_MAX_NAME_LENGTH);
		ret = read_sysfs_string("name", trig_dev_name, trigger_name);
		free(trig_dev_name);
		if (ret < 0) {
			fprintf(stderr, "Failed to read trigger%d name from\n", trig_num);
			return ret;
			}
	}
	/*
	 * Construct the directory name for the associated buffer.
	 * As we know that the ads1018 has only one buffer this may
	 * be built rather than found.
	 */
	ret = asprintf(&buf_dir_name,
		       "%siio:device%d/buffer", iio_dir, dev_num);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error;
	}

   	/* Build element name to be enabled */
	element_name = malloc(IIO_MAX_NAME_LENGTH);
	if (!element_name) {
		ret = -ENOMEM;
		goto error;
	}

	sprintf(element_name,"scan_elements/in_%s_en",pActualElement);
    
	ret = write_sysfs_int(element_name, dev_dir_name, 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to write %s\n",element_name);
		goto error;
	}
	ret = write_sysfs_int("scan_elements/in_timestamp_en", dev_dir_name, 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to write in_timestamp_en\n");
		goto error;
	}

 	/*
	 * Parse the files in scan_elements to identify what channels are
	 * enabled in this device.
	 * Must be done after element has been enabled.
	 */
	ret = build_channel_array(dev_dir_name, &channels, &num_channels);
 	if (ret) {
		fprintf(stderr, "Problem reading scan element information\n"
			"diag %s\n", dev_dir_name);
		goto error;
	}


	scan_size = size_from_channelarray(channels, num_channels);
	data = malloc(scan_size * buf_len);
	if (!data) {
		ret = -ENOMEM;
		goto error;
	}
    
	if (actualTrigger)
	{
		char sampleRateElementName[64];
		/*
		 * Set the device trigger to be the data ready trigger found
		 * above
		 */
		ret = write_sysfs_string_and_verify("trigger/current_trigger",
						dev_dir_name, trigger_name);

		if (ret < 0) {
			fprintf(stderr,
				"Failed to write current_trigger file\n");
			goto error;
		}

		/*
		 * Set the sample rate for the ads1018
		 */

		sprintf(sampleRateElementName, "in_%s_sampling_frequency",pActualElement);

		ret = write_sysfs_int_and_verify(sampleRateElementName,
						dev_dir_name, actualSampleRate);

		if (ret < 0) {
			fprintf(stderr,
				"Failed to write in_%s_sampling_frequency",pActualElement);
			goto error;
		}

	}


	/* For INA2XX devices the sample rate is controlled by setting vbus and 
         * shunt timings and setting number of samples to average.
	 * If they have changed from the default then set them individually
	 */
	if ((actualDeviceEnum==ina219) || (actualDeviceEnum==ina226))
	{
		unsigned int sampleRate;
		char vbus_string[12];

		if ((actualDeviceEnum==ina226) && (ina2xx_Vbus_sample_time_us != INA226_DEFAULT_SAMPLE_TIME_US))
		{		
			sprintf(vbus_string, "%06f\n",((double)ina2xx_Vbus_sample_time_us/1000000.0));
			printf("ina226 bus IT %s\n",vbus_string);
			ret = write_sysfs_string("in_voltage0_integration_time", dev_dir_name, vbus_string);
			if (ret < 0) {
				fprintf(stderr, "Failed to write in_voltage0_integration_time\n");
				goto error;
			}
		}

		if ((actualDeviceEnum==ina226) && (ina2xx_Vshunt_sample_time_us != INA226_DEFAULT_SAMPLE_TIME_US))
		{
			sprintf(vbus_string, "%06f\n",((double)ina2xx_Vshunt_sample_time_us/1000000.0));
			printf("ina226 shunt IT %s\n",vbus_string);
			ret = write_sysfs_string("in_voltage1_integration_time", dev_dir_name, vbus_string);
			if (ret < 0) {
				fprintf(stderr, "Failed to write in_voltage1_integration_time\n");
				goto error;
			}
		}

		if ((actualDeviceEnum==ina219) && (ina2xx_Vshunt_sample_time_us != INA219_DEFAULT_SAMPLE_TIME_US))
		{
			sprintf(vbus_string, "%06f\n",((double)ina2xx_Vshunt_sample_time_us/1000000.0));
			printf("ina219 shunt IT %s\n",vbus_string);
			ret = write_sysfs_string("in_voltage1_integration_time", dev_dir_name, vbus_string);
			if (ret < 0) {
				fprintf(stderr, "Failed to write in_voltage1_integration_time\n");
				goto error;
			}
		}

		if ((actualDeviceEnum==ina226) && (ina226_samples_to_average != INA226_DEFAULT_SAMPLE_AVERAGE))
		{
			printf("ina226 avg %d\n",ina226_samples_to_average);
			ret = write_sysfs_int("in_oversampling_ratio", dev_dir_name, ina226_samples_to_average);
			if (ret < 0) {
				fprintf(stderr, "Failed to write in_oversampling_ratio\n");
				goto error;
			}
		}

		/* Always read back sample rate derived from updated parameters */
		sampleRate=read_sysfs_posint("in_sampling_frequency", dev_dir_name);
		printf("INA2XX sample rate is %u\n",sampleRate);
	}


	/* Setup ring buffer parameters */
	ret = write_sysfs_int("length", buf_dir_name, buf_len);
	if (ret < 0)
		goto error;

	ret = write_sysfs_int("watermark", buf_dir_name, buf_watermark);
	if (ret < 0)
		goto error;

	/* Enable the buffer */
	ret = write_sysfs_int("enable", buf_dir_name, 1);
	if (ret < 0) {
		fprintf(stderr,
			"Failed to enable buffer: %s\n", strerror(-ret));
		goto error;
	}

	ret = asprintf(&buffer_access, "/dev/iio:device%d", dev_num);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error;
	}


	/* Attempt to open non blocking the access dev */
	fp = open(buffer_access, O_RDONLY | O_NONBLOCK);
	if (fp == -1) { /* TODO: If it isn't there make the node */
		ret = -errno;
		fprintf(stderr, "Failed to open %s\n", buffer_access);
		goto error;
	}

	/* Check if stop file has been written */
	if (stat("/var/www/stopCapture", &st) == -1) 
		stopFileDoesNotExist=true;
	else
		stopFileDoesNotExist=false;	
        
    
	while (stopFileDoesNotExist) {
		char 		*pSampleData;
		uint64_t 	TSmicrosecond;
		uint16_t 	previousTimeUs;
		int16_t  	adcValue;


		struct pollfd pfd = {
			.fd = fp,
			.events = POLLIN,
		};

		// Check the current time
		nowSinceEpoch = time(NULL);
		pNowDate=gmtime(&nowSinceEpoch);
 
		if (currentDay != pNowDate->tm_mday)
		{
			// It is a new day so close current database 
			closeSqliteDB();
                    
			// and create a new one with a new name
			sprintf(sqlDatabaseName,"/var/www/sql/currentCapture-%02d-%02d-%02d-%02d:%02d:%02d",pNowDate->tm_mday, pNowDate->tm_mon+1, 
                        pNowDate->tm_year+1900,pNowDate->tm_hour, pNowDate->tm_min, pNowDate->tm_sec);
                    
			// and open it
			if (openSqliteDB(sqlDatabaseName) == 1)
			{
				printf("ERROR: Unable to open SQL database\n");    
				return -1;
			}
                   
			// Set the current day so we can use to check for new day on next loop
			currentDay=pNowDate->tm_mday;
                    
		}

                
		ret = poll(&pfd, 1, -1);
		if (ret < 0) {
			ret = -errno;
			goto error;
		} else if (ret == 0) {
			continue;
		}

		toread = buf_len;

 		// This will block until the watermark number of samples are available
		read_size = read(fp, data, toread * scan_size);

		if (read_size < 0) {
			if (errno == EAGAIN) {
				fprintf(stderr, "nothing available\n");
				continue;
			} else {
				break;
			}
		}
		
		// At this point we have two channels
		// a 2 byte channel with the 16 bit sample
		// an 8 byte channel with the timestamp in ns 
		for (i = 0; i < read_size / scan_size; i++) {
			pSampleData=data + scan_size * i;

			adcValue=get2byte(*(int16_t *)(pSampleData + channels[0].location),&channels[0]);
			TSmicrosecond=getTimestampUs(*(uint64_t *)(pSampleData + channels[1].location),&channels[1]);

			// If it is the first sample in the blob then populate the header values
			if (i==0)
			{
				currentSamples.startTime=TSmicrosecond;
				currentSamples.scalingFactor=scalingFactorLSB;
				currentSamples.samplePeriod=SAMPLE_PERIOD_US;
				currentSamples.numberSamplesPerBlob=NUMBER_SAMPLES_PER_DB_WRITE;
				currentSamples.timeDeltaUs[i]=0;
				previousTimeUs=TSmicrosecond;
			}
			else
			{ 
				// Just store the value and the delta to previous samples
				currentSamples.timeDeltaUs[i]=(unsigned short) (TSmicrosecond-previousTimeUs);
				previousTimeUs=TSmicrosecond;
			}
			// Store actual data value
			currentSamples.ADCValues[i]=adcValue;
		}
           
           
		/* Write out the captured buffer to SQL */ 
		writeCurrentDataToSQL(currentSamples);

		/* Check if stop file has been written */
		if (stat("/var/www/stopCapture", &st) == -1) 
		{
			stopFileDoesNotExist=true;
		}
		else
		{
			stopFileDoesNotExist=false;
		}
	}


error:

	/* Disable the buffer */
	if (read_sysfs_posint("enable", buf_dir_name))
	{
		ret = write_sysfs_int("enable", buf_dir_name, 0);
		if (ret < 0) {
			fprintf(stderr,
				"Failed to disable buffer: %s\n", strerror(-ret));
			goto error;
		}
	}

	/* Disable the element that had been enabled so that another one could be enabled later */
	if (element_name && read_sysfs_posint(element_name, dev_dir_name))
	{
		ret = write_sysfs_int(element_name, dev_dir_name, 0);
		if (ret < 0) {
			fprintf(stderr, "Failed to disable %s\n",element_name);
		}
	}
	if (fp >= 0 && close(fp) == -1)
		perror("Failed to close buffer");
    
	// Close down the iio 
	cleanup();
   
	// Sleep for 1 second to allow IIO to stop
	usleep(1000000);
	fflush(stdout);
        
	checkBeforeFree(buffer_access);
	checkBeforeFree(data);
	checkBeforeFree(element_name);
	checkBeforeFree(buf_dir_name);
	for (i = num_channels - 1; i >= 0; i--) {
		checkBeforeFree(channels[i].name);
		checkBeforeFree(channels[i].generic_name);
	}
	checkBeforeFree(channels);
	if (actualTrigger) {
		checkBeforeFree(trigger_name);
	}
	checkBeforeFree(device_name);
	checkBeforeFree(dev_dir_name);
    
	checkBeforeFree(pActualDeviceStr);

	return ret;
 
}


