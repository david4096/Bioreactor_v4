/*****************************************************************************************
 This thread takes care of the logs and manage the time and its synchronisation 
 The thread write the logs at a definite fixed interval of time in the SST25VF064 chip 
 The time synchronization works through the NTP protocol and our server
******************************************************************************************/
#include <SST.h>

//Types of logs
#define ENTRY_SIZE_LINEAR_LOGS     64
#define NB_PARAMETERS_LINEAR_LOGS  26
#define SIZE_TIMESTAMPS            4
#define SIZE_COUNTER_ENTRY         4

// Definition of the log sectors in the flash for the logs
#define ADDRESS_BEG   0x000000
#define ADDRESS_MAX   0x800000 // http://www.sst.com/dotAsset/40498.pdf&usd=2&usg=ALhdy294tEkn4s_aKwurdSetYTt_vmXQhw
#define ADDRESS_LAST  (ADDRESS_MAX - ENTRY_SIZE_LINEAR_LOGS)
#define SECTOR_SIZE       4096
#define NB_ENTRIES_PER_SECTOR    (SECTOR_SIZE  / ENTRY_SIZE_LINEAR_LOGS)
#define ADDRESS_SIZE  (ADDRESS_MAX  - ADDRESS_BEG)
// The number of entires by types of logs (seconds, minutes, hours, commands/events)
#define MAX_NB_ENTRIES    (ADDRESS_SIZE  / ENTRY_SIZE_LINEAR_LOGS)

SEMAPHORE_DECL(lockFlashAccess, 1);
SST sst=SST(4);

uint32_t nextEntryID = 0;
boolean logActive=false;
boolean busy_flag=false;

/*******************
  SPI slave slect
*******************/
void selectFlash(){
  #ifdef FLASH_SELECT 
  digitalWrite(FLASH_SELECT,LOW);
  #endif
}

void deselectFlash(){
  #ifdef FLASH_SELECT 
  digitalWrite(FLASH_SELECT,HIGH);
  #endif
}

/*********************************************************************************** 
 Save logs in the Flash memory.
 event_number: If there is a command, then this parameter should be set with the
 corresponding command/event number. Should be found in the define list of
 commands/errors
************************************************************************************/
void writeLog() {
  writeLog(0,0);
}

void writeLog(uint16_t event_number, uint16_t parameter_value) {
  /********************************
             Safeguards
  ********************************/
  #if ! defined ( THR_LINEAR_LOGS ) 
  return;
  #endif
  if (!logActive) return;
  nilSemWait(&lockFlashAccess);
  /*****************************
            Slave Select
  ******************************/
  selectFlash();   
  delay(10);
  /************************************************************************************
    Test if it is the begining of one sector, erase the sector of 4096 bytes if needed
  ************************************************************************************/
  if((!(nextEntryID % NB_ENTRIES_PER_SECTOR))) {
    #ifdef DEBUG_LOGS
    Serial.print(F("ERASE sctr: "));
    Serial.println(findSectorOfN());
    #endif
    sst.flashSectorErase(findSectorOfN());
  }
  /*****************************
          Writing Sequence
  ******************************/
  uint16_t param = 0;
  sst.flashWriteInit(findAddressOfEntryN(nextEntryID)); // Initialize with the right address 
  sst.flashWriteNextInt32(nextEntryID);      //4 bytes of the entry number
  sst.flashWriteNextInt32(now());            //4 bytes of the timestamp in the memory using a mask
  for(byte i = 0; i < NB_PARAMETERS_LINEAR_LOGS; i++) {
    param = getParameter(i);
    sst.flashWriteNextInt16(param);          //2 bytes per parameter
  }
  sst.flashWriteNextInt16(event_number);    //event
  sst.flashWriteNextInt16(parameter_value); //parameter value
  sst.flashWriteFinish();                   // finish the writing process
  /*****************************
          Check Sequence
  ******************************/
  sst.flashReadInit(findAddressOfEntryN(nextEntryID));
  long writtenID=sst.flashReadNextInt32();
  sst.flashReadFinish();
  if (writtenID==nextEntryID) {
    //Update the value of the next event log position in the memory
    nextEntryID++;
    #ifdef DEBUG_LOGS
    Serial.print(F("nextEntry "));
    Serial.println(nextEntryID);
    #endif
    sst.flashSectorErase(findSectorOfN());
  }
  
  #ifdef DEBUG_LOGS
  else
    Serial.print(F("Write Fail"));
  #endif
  /*****************************
         Out and Deselect
  ******************************/
  nilThdSleepMilliseconds(5);
  nilSemSignal(&lockFlashAccess);
  deselectFlash();
}

/******************************************************************************************
 Read the corresponding logs in the flash memory of the entry number (ID).
 result:          Array of uint8_t where the logs are stored. It should be a 32 bytes array
 for the 3 RRD logs and 12 bytes for the commands/events logs.  
 *entryN: Log ID that will correspond to the logs address to be read and stored in result
 return:  Error flag: 0: no error occured
 EVENT_ERROR_NOT_FOUND_ENTRY_N: The log ID (entryN) was not found in the flash memory
 *****************************************************************************************/
uint32_t printLogN(Print* output, uint32_t entryN) {

  nilSemWait(&lockFlashAccess);
  selectFlash();

  if ((nextEntryID > MAX_NB_ENTRIES) && (entryN < (nextEntryID - MAX_NB_ENTRIES + NB_ENTRIES_PER_SECTOR))) // Start with the first that is on the card and skip a sector
    entryN=nextEntryID - MAX_NB_ENTRIES + NB_ENTRIES_PER_SECTOR;
  
  uint32_t addressOfEntryN = findAddressOfEntryN(entryN);

  #ifdef DEBUG_LOGS
  Serial.print(F("entryN: "));
  Serial.println(entryN);
  Serial.print(F("addrN: "));
  Serial.println(addressOfEntryN);
  #endif

  sst.flashReadInit(addressOfEntryN);

  byte checkDigit=0;
  for(byte i = 0; i < ENTRY_SIZE_LINEAR_LOGS; i++) {
    byte oneByte=sst.flashReadNextInt8();
    checkDigit^=toHex(output, oneByte);
  }
  
  checkDigit^=toHex(output, (int)getQualifier());
  toHex(output, checkDigit);
  output->println("");

  sst.flashReadFinish();
  nilSemSignal(&lockFlashAccess);
  deselectFlash();
  return entryN;
}


uint8_t loadLastEntryToParameters() {
  nilSemWait(&lockFlashAccess);
  selectFlash();
  uint32_t addressOfEntryN = findAddressOfEntryN(nextEntryID-1);
  sst.flashReadInit(addressOfEntryN+8); // we skip entryID and epoch

  for(byte i = 0; i < NB_PARAMETERS_LINEAR_LOGS; i++) {
    setParameter(i,sst.flashReadNextInt16());
  }

  sst.flashReadFinish();
  nilSemSignal(&lockFlashAccess);
  deselectFlash();
}


/*************************************************************************************
 The flash memory is implemented with sectors of a defined size.
 The function returns the sector number where the log corresponding to the ID (entryNb) 
 is stored in the flash memory
 entryNb:         The log ID
 return:          The sector number
**************************************************************************************/
uint16_t findSectorOfN( ) {
  uint16_t sectorNb = 0;
  uint32_t address = findAddressOfEntryN(nextEntryID);
  sectorNb = address / SECTOR_SIZE;
  return sectorNb;
}


/******************************************************************************
 Returns the address corresponding to one log ID
 entryNb:     Log ID 
 return:      Address of the first byte where the corresponding log is located
*******************************************************************************/
uint32_t findAddressOfEntryN(uint32_t entryN)
{
  uint32_t address = ((entryN % MAX_NB_ENTRIES) * ENTRY_SIZE_LINEAR_LOGS) % ADDRESS_SIZE + ADDRESS_BEG;
  return address;
}

/*****************************************************************************
 Returns the last log ID stored in the memory 
 return: Last log ID stored in the memory corresponding to a log type
******************************************************************************/
void recoverLastEntryN() 
{
  selectFlash();
  uint32_t ID_temp = 0;
  uint32_t Time_temp = 0;
  uint32_t addressEntryN = ADDRESS_BEG;
  boolean found = false;

  #ifdef DEBUG_LOGS
  Serial.print(F("1st addr: "));
  Serial.println(ADDRESS_BEG);
  Serial.print(F("Max addr: "));
  Serial.println(ADDRESS_LAST);
  #endif

  while(addressEntryN<ADDRESS_LAST) 
  {
    sst.flashReadInit(addressEntryN);
    ID_temp = sst.flashReadNextInt32();
    Time_temp = sst.flashReadNextInt32(); 
    sst.flashReadFinish();          
    
    #ifdef DEBUG_LOGS    
    Serial.print(F("ID_tmp: "));
    Serial.println(ID_temp);
    Serial.print(F("nextEntryID: "));
    Serial.println(nextEntryID);
    #endif
    
    // Test if first memory slot contains any information
    if((ID_temp == 0xFFFFFFFF) || (ID_temp < nextEntryID))
    {
      break;
    }
    addressEntryN += ENTRY_SIZE_LINEAR_LOGS;
    nextEntryID = ID_temp+1; // this will be the correct value in case of break
    setTime(Time_temp);

    // we implement a quick advance
    if (addressEntryN<(ADDRESS_LAST-128*ENTRY_SIZE_LINEAR_LOGS)) {
      sst.flashReadInit(addressEntryN+(128*ENTRY_SIZE_LINEAR_LOGS));
      ID_temp = sst.flashReadNextInt32();
      sst.flashReadFinish(); 
      if (ID_temp >= nextEntryID && ID_temp != 0xFFFFFFFF) {
        addressEntryN+=127*ENTRY_SIZE_LINEAR_LOGS;
      }
    }

    #ifdef DEBUG_LOGS
    Serial.print(F("Current nextEntryID:")); 
    Serial.println(nextEntryID);
    #endif
  }
  #ifdef DEBUG_LOGS
  Serial.print(F("Final nextEntryID:")); 
  Serial.println(nextEntryID);
  #endif
  logActive=true;
  deselectFlash();
  }

/*****************************
 Memory related functions
 *****************************/
//Setup the memory for future use
//Need to be used only onced at startup
void setupMemory(){
  SPI.begin();
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  sst.init();
}

void printLastLog(Print* output) {
  printLogN(output, nextEntryID-1);
}

void formatFlash(Print* output) {
  busy_flag=true;
  setupMemory();
  output->println(F("Format flash"));
  output->print(F("Sctr size: "));
  output->println(SECTOR_SIZE);
  output->print(F("Nb sctrs: "));
  output->println(ADDRESS_MAX/SECTOR_SIZE);
  wdt_disable();
  for (int i=0; i<ADDRESS_MAX/SECTOR_SIZE; i++) {
    selectFlash();
    sst.flashSectorErase(i);
    if (i%16==0)
      output->print(".");
    if (i%1024==1023)
      output->println(""); 
    nilThdSleepMilliseconds(10);
  } 
  deselectFlash();
  wdt_enable(WDTO_8S);
  wdt_reset();
  output->println(F("OK"));
  setTime(0);
  nextEntryID=0;
  busy_flag=false;
}

//need revision !!!
void validateFlash(Print* output) {
  logActive=false;
  formatFlash(output);
  output->println(F("Write / read / validate"));
  for (int i=0; i<ADDRESS_MAX/SECTOR_SIZE; i++) {
    for (byte j=0; j<SECTOR_SIZE/64; j++) {
      long address=(long)i*SECTOR_SIZE+(long)j*64;
      sst.flashWriteInit(address);
      byte result=0;
      for (byte k=0; k<64; k++) {
        result^=(k+13);
        sst.flashWriteNextInt8(k+13);
      }
      sst.flashWriteFinish();
      sst.flashReadInit(address);
      for (byte k=0; k<64; k++) {
        result^=sst.flashReadNextInt8();
      }
      sst.flashReadFinish();
      if (result==0) {
        if (j==0 && i%16==0) {
          output->print(".");
        }
        if (j==0 && i%1024==1023) {
          output->println(""); 
        }
      } 
      else {/*
        output->print(F("Bad addr: "));*/
        output->println(address);
      }
    }
  }
  formatFlash(output);
  nextEntryID=0;
}

#ifdef LOG_INTERVAL

NIL_WORKING_AREA(waThreadLogger, 0);
NIL_THREAD(ThreadLogger, arg) {
  writeLog(EVENT_ARDUINO_BOOT,0);
  while(TRUE) {
    nilThdSleepMilliseconds(LOG_INTERVAL*1000-millis()%1000+100); //by default the time is too short, we add 100ms not to have rounding pro
    if(!busy_flag)
      writeLog();
  }
}

#endif




















