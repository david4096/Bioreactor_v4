/*********************************************
 * This file is used to declare the parameters
 * table used by the program.
 * 
 * We use the EEPROM for saving the parameters 
 * changed by the user during the functionment 
 * of the Bioreactor.
 * 
 * The parameter are loaded during the boot.
 * 
 * All change to important parameters are saved 
 * to the EEPROM
 *********************************************/

#include <avr/eeprom.h>


#define EEPROM_MIN_ADDR            0
#define EEPROM_MAX_ADDR          511
#include <EEPROM.h> // should be removed and based only on avr/eeprom

#define MAX_PARAM 52   // If the MAX_PARAM change you need to change the pointer in the EEPROM


//When parameters are set (and saved) an event is recorded (256-281 : A-Z + .... (if more parameters than 26))
#define EVENT_SAVE_ALL_PARAMETER     255
#define EVENT_PARAMETER_SET          256


#define EE_START_PARAM           0 // We save the parameter from byte 0 of EEPROM
#define EE_LAST_PARAM            (MAX_PARAM*2-1) // The last parameter is stored at byte 50-51

#define EEPROM_MIN_ADDR            0
#define EEPROM_MAX_ADDR          511


#define EE_LORA_DEVADDR          150  // 8 bytes
#define EE_LORA_NWKSKEY          158  // 32 bytes
#define EE_LORA_APPSKEY          190  // 32 bytes
#define EE_QUALIFIER             222


// value that should not be taken into account
// in case of error the parameter is set to this value
#define ERROR_VALUE  -32768

int parameters[MAX_PARAM];

void setupParameters() {
  //We copy all the value in the parameters table
  eeprom_read_block((void*)parameters, (const void*)EE_START_PARAM, MAX_PARAM*2);
}

int getParameter(byte number) {
  return parameters[number];
}

void setParameterBit(byte number, byte bitToSet) {
   bitSet(parameters[number], bitToSet);
  // parameters[number]=parameters[number] | (1 << bitToSet);
}

void clearParameterBit(byte number, byte bitToClear) {
   bitClear(parameters[number], bitToClear);
  // parameters[number]=parameters[number] & ( ~ (1 << bitToClear));
}

byte getParameterBit(byte number, byte bitToRead) {
   return bitRead(parameters[number], bitToRead);
  // return (parameters[number] >> bitToRead ) & 1;
}

void setParameter(byte number, int value) {
  parameters[number]=value;
}

void saveParameters() {
  for (byte i=0; i<MAX_PARAM; i++) {
    eeprom_write_word((uint16_t*) EE_START_PARAM+i, parameters[i]);
  }
  writeLog(EVENT_SAVE_ALL_PARAMETER, 0);
}

/*
This will take time, around 4 ms
 This will also use the EEPROM that is limited to 100000 writes
 */
void setAndSaveParameter(byte number, int value) {
  parameters[number]=value;
  //The address of the parameter is given by : EE_START_PARAM+number*2
  eeprom_write_word((uint16_t*) EE_START_PARAM+number, value);

  writeLog(EVENT_PARAMETER_SET+number, value);
}


void printParameter(Print* output, byte number){
  output->print(number);
  output->print("-");
  if (number>25) {
    output->print((char)(floor(number/26) + 64));
  } else {
    output->print(" ");
  }
  output->print((char)(number-floor(number/26)*26 + 65));
  output->print(": ");
  output->println(parameters[number]);
}

void printParameters(Print* output) {
  for (byte i = 0; i < MAX_PARAM; i++) {
    printParameter(output, i);
  }
}


// code from http://www.arduino.cc/playground/Code/EepromUtil
void getStatusEEPROM(Print* output) {
  int bytesPerRow = 16;
  int i;
  int j;
  byte b;
  char buf[4];

  j = 0;

  // go from first to last eeprom address
  for (i = EEPROM_MIN_ADDR; i <= EEPROM_MAX_ADDR; i++) {
    if (j == 0) {
      sprintf(buf, "%03X", i);
      output->print(buf);
      output->print(F(": "));
    }

    // read current byte from eeprom
    b = EEPROM.read(i);
    sprintf(buf, "%02X ", b);
    j++;
    if (j == bytesPerRow) {
      j = 0;
      output->println(buf);
      nilThdSleepMilliseconds(25);
    }
    else {
      output->print(buf);
    }
  }
}


uint8_t printCompactParameters(Print* output) {
  printCompactParameters(output, MAX_PARAM);
}

uint8_t printCompactParameters(Print* output, byte number) {
  if (number > MAX_PARAM) {
    number=MAX_PARAM;
  }
  byte checkDigit=0;

  // we first add epoch
  checkDigit^=toHex(output, (long)now());
  for(int i = 0; i < number; i++) {
    int value=getParameter(i);
    checkDigit^=toHex(output, value);
  }
  checkDigit^=toHex(output, (int)getQualifier());
  toHex(output, checkDigit);
  output->println("");
}

/* The qualifier represents the card ID and is stored just after the last parameter */
uint16_t getQualifier() {
  return eeprom_read_word((uint16_t*)(EE_QUALIFIER));
}

void setQualifier(uint16_t value) {
  eeprom_write_word((uint16_t*)(EE_QUALIFIER), value);
}


