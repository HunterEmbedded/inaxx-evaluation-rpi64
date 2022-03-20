#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <sqlite3.h>       /* SQLite3 API.         */

#include <time.h>

#define NUMBER_SAMPLES_PER_DB_WRITE 100

typedef struct t_logData {
       unsigned long long 	startTime;
       float 			scalingFactor;
       unsigned int		samplePeriod;
       unsigned int		numberSamplesPerBlob;
       short	 		ADCValues[NUMBER_SAMPLES_PER_DB_WRITE];  /* message data samples */
       short	 		timeDeltaUs[NUMBER_SAMPLES_PER_DB_WRITE];  /* delta between samples in us */
}t_logData;



int writeCurrentDataToSQL(t_logData sampleData);
int openSqliteDB(char *dataBaseName);
void closeSqliteDB(void);
int checkPointDB(void);

