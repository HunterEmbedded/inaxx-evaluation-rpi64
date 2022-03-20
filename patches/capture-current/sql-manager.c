#include "sql-manager.h"

/* Pointer to database */
static sqlite3 *pSqliteDB;


/* Assumption here is that each database is unique */ 
int openSqliteDB(char *dataBaseName)
{
   int retVal;
   char sqlCommand[200];

   // Check if database file exists, and return with error if it does 
   if (access(dataBaseName, F_OK) != -1)
     {
      return (-1);
    }
      
   /* open SQLite db */
   retVal = sqlite3_open_v2(dataBaseName,&pSqliteDB,SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX , NULL);
   if( retVal ){
       fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(pSqliteDB));
       sqlite3_close(pSqliteDB);
       return(-1);
     }
#if 0  
   // On opening switch to write-ahead-logging. This allows multiple readers and writers
   retVal = sqlite3_exec(pSqliteDB,"pragma journal_mode = WAL", NULL, NULL, NULL);
   if( retVal ){
       fprintf(stderr, "Can't set WAL mode: %s\n", sqlite3_errmsg(pSqliteDB));
       sqlite3_close(pSqliteDB);
       return(-1);
    }
#endif
   printf("Opened DB  %s\n",dataBaseName);


   // Set a busy timeout of 1000ms on the database to try to prevent error due to table being locked
   sqlite3_busy_timeout(pSqliteDB,1000);


   /* There is one table used in SQL */
   /* currentData - contains current in uA */
   printf("openSqliteDb() FORMAT db\n");
 
   /* Create the node_data table */
   strcpy(sqlCommand, "CREATE TABLE currentData(startTimeOfBlob INTEGER, scalingFactor REAL, sampleRate INTEGER, numberOfSamplesPerBlob INTEGER, currentInuA BLOB, timeDeltaUs BLOB);");  
   retVal = sqlite3_exec(pSqliteDB,sqlCommand, NULL, NULL, NULL);
   if( retVal ){
      fprintf(stderr, "Can't format database1: %s\n", sqlite3_errmsg(pSqliteDB));
      sqlite3_close(pSqliteDB);
      return(-1);
    }

   printf("Formatted DB %s\n",dataBaseName);


   return 0;
}

int writeCurrentDataToSQL(t_logData sampleData)
{
    char *sqlCommand="INSERT INTO currentData VALUES (?1, ?2, ?3, ?4, ?5, ?6)";
    int retVal; 
    sqlite3_stmt *insert_stmt;
 

    retVal = sqlite3_prepare_v2(pSqliteDB, sqlCommand, -1, &insert_stmt, 0);
    if( retVal!=SQLITE_OK ){
         fprintf(stderr, "Can't prepare  %s\n", sqlite3_errmsg(pSqliteDB));
     }


    sqlite3_bind_int64(insert_stmt, 1, sampleData.startTime);
    sqlite3_bind_double(insert_stmt, 2, sampleData.scalingFactor);
    sqlite3_bind_int(insert_stmt, 3, sampleData.samplePeriod);
    sqlite3_bind_int(insert_stmt, 4, sampleData.numberSamplesPerBlob);
    sqlite3_bind_blob(insert_stmt, 5, sampleData.ADCValues,sampleData.numberSamplesPerBlob*sizeof(short), SQLITE_STATIC);
    sqlite3_bind_blob(insert_stmt, 6, sampleData.timeDeltaUs,sampleData.numberSamplesPerBlob*sizeof(unsigned short), SQLITE_STATIC);

    retVal = sqlite3_step(insert_stmt);
    if (retVal != SQLITE_DONE)
    {
        fprintf(stderr, "Can't insert blob into db %s\n", sqlite3_errmsg(pSqliteDB));
        return -1;
     }

    sqlite3_finalize(insert_stmt);

    return (0);
}


void closeSqliteDB(void)
{  int ret;
   ret=sqlite3_close(pSqliteDB); 
   printf("sqlite3_close() returned %d\n",ret);

   // If it was busy then loop until finished
   if (ret == SQLITE_BUSY)
     {
     do{

        sleep(1);
        ret=sqlite3_close(pSqliteDB); 
        printf("sqlite3_close() returned %d\n",ret);
       }while(ret == SQLITE_BUSY);
 
    }
}

/*
 * Checkpoint db to write back all data from WAL
 */
int checkPointDB(void)
{   int logSize,framesCheckpointed;
    int ret;
    
    ret=sqlite3_wal_checkpoint_v2(pSqliteDB,NULL,SQLITE_CHECKPOINT_FULL,&logSize,&framesCheckpointed);

    printf("checkPointDB() %s, logSize %d frames checkpointed %d",sqlite3_errmsg(pSqliteDB),
    		logSize,framesCheckpointed);
    return ret;
}

