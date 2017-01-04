/* This runs a word clock using an Arduino Uno microprocessor as controller,
	a DS1305 Real-Time Clock Chip,
	6 TLC5916 8-bit LED Driver Chips,
	and two NO momentary push-buttons to drive a 100 LED Word Clock that
	outputs time to the nearest five minute increment and has a series of
	COM-12999 RGB LEDs with a built-in WS2812 chips to control the color
	using signaling instead of discrete outputs for each LED.  The LEDs are
	lit in a matrix to back-light a laser cut board where the time can be
	read in words.  The RGB LEDs are used to backlight a section for happy
	birthday NAME only on that date of the year read from the Real-time
	clock chip.  The two buttons are used to set the minute, hour, day,
	month, and year of the clock.

	When powered up, the arduino will read the RTC and display the current
	time.  Holding the top button for 2s will enter change mode starting
	with hour.  Pressing the top button again will cycle the change options
	through hour, minute, day, month, year.  Holding the top button again
	for 2s will write these changes to the RTC chip.  On the hard-coded
	birthday for this clock, the RGB LEDs will be instructed to rainbow
	cycle the entire duration of that day, then turn back off when the
	real-time clock indicates a date mismatch.
	*/

/*
todo:
*/


#include <Arduino.h>
#include <SPI.h>
#include "COM12999_NeoPixel.h"

//function declarations
void displayTime();

//Constants needed for the entire program
//The Arduino Pins used for Slave Selects on SPI modes
#define RTCSlave 8
#define LEDSlave 9
//The Arduino Pin used for RGB WS2812 output
#define RGBOutput 7
//The Arduino Pins used for button inputs
#define changeModeButton 4
#define changeItemButton 3

//Neopixel strip information
COM12999_NeoPixel strip = COM12999_NeoPixel(20, RGBOutput, NEO_GRB + NEO_KHZ800);

//Clock Output Masks in single byte form.  These have to be or'd together into a
//48 bit word to output to the 6 chips.
byte ONE = 0x04; //in 3LED2
byte TWO = 0x04; //in 3LED1
byte THR = 0x10; //in 3LED1
byte EE = 0x01;  //in 2LED
byte FOUR = 0x04; //in 4LED1
byte FIVEbot = 0x20; //in 4LED1
byte SIX = 0x02; //in 3LED2
byte SEV = 0x20; //in 3LED2
byte EN = 0x02; //in 2LED
byte EIG = 0x08; //in 3LED2
byte HT = 0x04; //in 2LED
byte NINE = 0x01; //in 4LED1
byte TENbot = 0x08; //in 3LED1
byte ELE = 0x10; //in 3LED2
byte VEN = 0x20; //in 3LED1
byte TWELVE = 0x42; //in 3LED1
byte OCL = 0x01; //in 3LED1
byte OCK = 0x80; //in 3LED2
byte ITIS = 0x02; //in 4LED2
byte TENtop = 0x01; //in 3LED2
byte HALF = 0x40; //in 4LED1
byte AQUA = 0x01; //in 4LED2
byte RTER = 0x80; //in 4LED1
byte TWEtop = 0x40; //in 3LED2
byte NTY = 0x80; //in 3LED1
byte FIVEtop = 0x02; //in 4LED1
byte MINU = 0x10; //in 4LED1
byte TES = 0x01; //in 3LED3
byte PAST = 0x08; //in 4LED1
byte TO = 0x08; //in 2LED

//Read address bytes for the clock. Writing is these addresses with a leading 1 bit
byte RTCMinute = 0x01; //returned in BCD format 00-59
byte RTCHour = 0x02; // returned in BCD kinda.  Bit 6 is to set 12/24 hr time, Bit 5 is either
					 // 24 hr clock hours or AM/PM depending on how Bit 6 is set
					 // values are 01-12 A/P or 00-23 depending on how Bit 6 is Set
byte RTCDate = 0x04; //returned in BCD 01-31
byte RTCMonth = 0x05; // returned in BCD 01-12
byte RTCYear = 0x06; // returned in BCD 00-99
byte RTCCtrl = 0x0F;
byte RTCStatus = 0x10;

//Here's the birthday specified in BCD for the clock
byte birthDate = 0x01;
byte birthMonth = 0x01;

//a boolean flag to determine if we show the rgb colorwheel
boolean birthday = false;

//some unsigned long timer counters for the main loop
//they are declared here so that they can used by all iterations of the loop() function
//without resetting their values
unsigned long rtcReadTimer = 0;
unsigned long changeModeButtonTimer = 0;
unsigned long RGBTimer = 0;

//now some boolean variables to track button presses
boolean changeModeButtonReading = false;
boolean changeModeButtonState = false;
boolean changeModeButtonLastState = false;

//need a color tracker for the birthday wheel
int colorTrack = 0;

//Initilize the SPI interface, digital interface pin modes, and serial interface
void setup() {
  Serial.begin(9600);  //begin serial communication with arduino

  //initialize the RTC and LED select pins as outputs, as well as the Arduino's SlaveSelect Pin
  pinMode(RTCSlave, OUTPUT);
  pinMode(LEDSlave, OUTPUT);
  pinMode(10, OUTPUT);  //Set the Arduino's Slave Pin to Output so it cant be accidently put into slave mode
  pinMode(RGBOutput, OUTPUT);
  pinMode(changeModeButton, INPUT);
  pinMode(changeItemButton, INPUT);

  //Set the SPI slave select for the clock to low since it isn't selected
  digitalWrite(RTCSlave,LOW);
  digitalWrite(LEDSlave,LOW);
  digitalWrite(10, LOW);  //Be sure to set the arduino's slave select pin to low

  SPI.begin();  //begin the SPI functions in the arduino.
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE3);
  SPI.setClockDivider(SPI_CLOCK_DIV8);

  //initialize the RGB strip to off
  strip.begin();
  strip.show();

  //start the clock oscillator
  writeClock(0b00001111, 0b00000000); //clear the write protect
  writeClock(0b00001111, 0b00000000); //start the oscillator (automatically turned off on power on)
  writeClock(0b00001111, 0b01000000); //reenable the write protect

  //display the time since the timers will elapse 60 seconds in the  main loop before the time is displayed again
  displayTime();
  // set the rtcReadTimer since we just displayed the time
  rtcReadTimer = millis();
}

//write a value into an address in the clock.  Any valid address and value can be specified
void writeClock(byte address, byte output) {
  //Write the address first, but it has to lead with a 1 in the MSB
  //of the address written to in the DS1305
  byte writeAddress = address | 0b10000000; //Add in the leading 1 for writing by bitwise ORing
  digitalWrite(RTCSlave, HIGH); //Set the Chip Enable High
  SPI.transfer(writeAddress); //Send the address for writing
  SPI.transfer(output); //Send the byte to load into the address
  digitalWrite(RTCSlave, LOW); //Set the Chip Enable LOW again
  return;  //return whatever the chip has in it
}

//read one byte from the specified address in the clock and return that byte
byte readClock(byte address) {
  byte incomingByte = 0;  //Set up a byte to read in the incoming value
  digitalWrite(RTCSlave, HIGH);  //Select the slave we're reading from
  SPI.transfer(address);  //Write the address to be read out to the slave
  incomingByte = SPI.transfer(0b00000000);  //Read in the returned value from the slave
  digitalWrite(RTCSlave, LOW);  //Deselect the slave
  return(incomingByte);  //Return the value given by the slave
}

//This function takes an input of 1 byte encoded in BCD and returns
//the unsigned integer value of that byte.
unsigned int BCDtoInt(byte input) {
  unsigned int value = 0;
  byte BCD = 0;
  BCD = input;
  BCD = BCD & 0b00001111; //Remove the Most Significant Nibble
  value = BCD;
  BCD = input;
  BCD = BCD >> 4;  //Shift the Nibble down
  value = value + (BCD*10);  //Add the second nibble in as the tens value
  return value;
}

//This function will take an input of unsigned int and convert it into a single BCD byte
//this assumption is made for 1 byte output because no parts of this clock will have
//acceptable values greater than one BCD byte
byte InttoBCD(unsigned int input) {
	byte value = 0x00;
	//take care of unexpected values by using modulo 100 to remove any int value greater than 99;
	input = input % 100;
	//first input the value of the integer divided by 10.
	//this will put the tens value in the least significant nibble of the byte.
	value = value | (input / 10);
	//shift that value into the top four bits for the tens place
	value = value << 4;
	//now load the least significant nibble with the ones place by using modulo 10 on the input
	value = value | (input % 10);
	//the byte should now correctly be converted into a BCD byte of the input integer so return it
	return value;
}

//This function reads the total time from the clock and returns a word with that data
//encoded as BCD.  Hour is the MSB minute is the LSB
word readTime() {
	byte minutes = 0x00;
	byte hours = 0x00;
	word curTime = 0x0000;
	minutes = readClock(RTCMinute);
	hours = readClock(RTCHour);
	curTime = word(hours, minutes);
	return curTime;
}

//compare the current date in the RTC with the set birthdate for this clock.
//Return 1 if it matches, return 0 otherwise
boolean compareDate() {
	byte date = 0x00;
	byte month = 0x00;
	date = readClock(RTCDate);
	month = readClock(RTCMinute);
	if ((date == birthDate) && (month = birthMonth)) {
		return 1;
	}
	return 0;
}

//This function outputs 48 bits to the LED Drivers based upon the BCD time
//returned from the readTime() function
void displayTime() {
	word curTime = 0x0000;
	curTime = readTime();
	byte minutes = lowByte(curTime);
	byte hours = highByte(curTime);

        Serial.print("Current Time: ");
        Serial.print(hours, HEX);
        Serial.print(":");
        Serial.println(minutes, HEX);

	//these are the actual bytes that will be written out to the LED drivers
	byte LED33 = 0x00;
	byte LED32 = 0x00;
	byte LED31 = 0x00;
	byte LED42 = 0x00;
	byte LED41 = 0x00;
	byte LED2 = 0x00;

	//Mask in IT IS since it is always on
	//Since the bytes start at 0x00, use bitwise OR to add in the 1s
	LED42 = LED42 | ITIS;

	//build the LED words based upon the minutes of the hour
	if ((0x00 <= minutes) && (minutes <= 0x04)) {
		//since we're at the top of the hour, just turn on OCLOCK, no minutes
		LED31 = LED31 | OCL; //add the OCLOCK mask
		LED32 = LED32 | OCK;
	}
	if ((0x05 <= minutes) && (minutes <= 0x09)) {
		//FIVE minutes PAST
		LED41 = LED41 | FIVEtop; //add the FIVE mask
		LED41 = LED41 | MINU; //add in the MINUTES mask
		LED33 = LED33 | TES;
		LED41 = LED41 | PAST; //add in the PAST mask
	}
	if ((0x10 <= minutes) && (minutes <= 0x14)) {
		//TEN minutes PAST
		LED32 = LED32 | TENtop; //add in the TEN mask for minutes
		LED41 = LED41 | MINU; //add in the MINUTES mask
		LED33 = LED33 | TES;
		LED41 = LED41 | PAST; //add in the PAST mask
	}
	if ((0x15 <= minutes) && (minutes <= 0x19)) {
		//AQUARTER PAST
		LED42 = LED42 | AQUA; //add in the AQUARTER mask
		LED41 = LED41 | RTER;
		LED41 = LED41 | PAST; //add in the PAST mask
	}
	if ((0x20 <= minutes) && (minutes <= 0x24)) {
		//TWENTY MINUTES PAST
		LED32 = LED32 | TWEtop; //add in the TWENTY mask
		LED31 = LED31 | NTY;
		LED41 = LED41 | MINU; //add in the MINUTES mask
		LED33 = LED33 | TES;
		LED41 = LED41 | PAST; //add in the PAST mask
	}
	if ((0x25 <= minutes) && (minutes <= 0x29)) {
		//TWENTY FIVE MINUTES PAST
		LED32 = LED32 | TWEtop; //add in the TWENTY mask
		LED31 = LED31 | NTY;
		LED41 = LED41 | FIVEtop; //add the FIVE mask
		LED41 = LED41 | MINU; //add in the MINUTES mask
		LED33 = LED33 | TES;
		LED41 = LED41 | PAST; //add in the PAST mask
	}
	if ((0x30 <= minutes) && (minutes <= 0x34)) {
		//HALF PAST
		LED41 = LED41 | HALF;//add in the HALF mask
		LED41 = LED41 | PAST; //add in the PAST mask
	}
	if ((0x35 <= minutes) && (minutes <= 0x39)) {
		//TWENTY FIVE MINUTES TO
		LED32 = LED32 | TWEtop; //add in the TWENTY mask
		LED31 = LED31 | NTY;
		LED41 = LED41 | FIVEtop; //add the FIVE mask
		LED41 = LED41 | MINU; //add in the MINUTES mask
		LED33 = LED33 | TES;
		LED2 = LED2 | TO; //add in the TO mask
	}
	if ((0x40 <= minutes) && (minutes <= 0x44)) {
		//TWENTY MINUTES TO
		LED32 = LED32 | TWEtop; //add in the TWENTY mask
		LED31 = LED31 | NTY;
		LED41 = LED41 | MINU; //add in the MINUTES mask
		LED33 = LED33 | TES;
		LED2 = LED2 | TO; //add in the TO mask
	}
	if ((0x45 <= minutes) && (minutes <= 0x49)) {
		//AQUARTER TO
		LED42 = LED42 | AQUA; //add in the AQUARTER mask
		LED41 = LED41 | RTER;
		LED2 = LED2 | TO; //add in the TO mask
	}
	if ((0x50 <= minutes) && (minutes <= 0x54)) {
		//TEN minutes TO
		LED32 = LED32 | TENtop; //add in the TEN mask for minutes
		LED41 = LED41 | MINU; //add in the MINUTES mask
		LED33 = LED33 | TES;
		LED2 = LED2 | TO; //add in the TO mask
	}
	if (0x55 <= minutes) {
	    //FIVE minutes TO
		LED41 = LED41 | FIVEtop; //add the FIVE mask
		LED41 = LED41 | MINU; //add in the MINUTES mask
		LED33 = LED33 | TES;
		LED2 = LED2 | TO; //add in the TO mask
	}

	//build the LED words based upon the hour
	switch (hours & 0x1F) {
		case (0x01):
    //ONE
    if (minutes <= 0x34) {
			LED32 = LED32 | ONE; //add in the ONE mask for first half of hour
    }
    else {
      LED31 = LED31 | TWO; //add in the TWO mask for second half of hour
    }
			break;
		case (0x02):
			//TWO
      if (minutes <= 0x34) {
			     LED31 = LED31 | TWO; //add in the TWO mask for first half of hour
      }
      else {
           LED31 = LED31 | THR; //add in the THREE mask for second half of hour
     	     LED2 = LED2 | EE;
      }
			break;
		case (0x03):
			//THREE
      if (minutes <= 0x34) {
		      LED31 = LED31 | THR; //add in the THREE mask for first half of hour
			    LED2 = LED2 | EE;
      }
      else {
        LED41 = LED41 | FOUR; //FOUR in the second half of the hour
      }
			break;
		case (0x04):
			//FOUR
      if (minutes <= 0x34) {
			     LED41 = LED41 | FOUR; //add in the FOUR mask
      }
      else {
        LED41 = LED41 | FIVEbot; //add in the FIVE hour mask
      }
			break;
		case (0x05):
			//FIVE
      if (minutes <= 0x34) {
			     LED41 = LED41 | FIVEbot; //add in the FIVE hour mask
      }
      else {
        LED32 = LED32 | SIX; //add in the SIX mask
      }
			break;
		case (0x06):
			//SIX
      if (minutes <= 0x34) {
			     LED32 = LED32 | SIX; //add in the SIX mask
      }
      else {
        LED32 = LED32 | SEV; //add in the SEVEN mask
        LED2 = LED2 | EN;
      }
			break;
		case (0x07):
			//SEVEN
      if (minutes <= 0x34) {
		     LED32 = LED32 | SEV; //add in the SEVEN mask
         LED2 = LED2 | EN;
      }
      else {
        LED32 = LED32 | EIG; //add in the EIGHT mask
        LED2 = LED2 | HT;
      }
			break;
		case (0x08):
			//EIGHT
      if (minutes <= 0x34) {
			     LED32 = LED32 | EIG; //add in the EIGHT mask
           LED2 = LED2 | HT;
      }
      else {
        LED41 = LED41 | NINE; //add in the NINE mask
      }
			break;
		case (0x09):
			//NINE
      if (minutes <= 0x34) {
			     LED41 = LED41 | NINE; //add in the NINE mask
      }
      else {
        LED31 = LED31 | TENbot; //add in the TEN hour mask
      }
			break;
		case (0x10):
			//TEN
      if (minutes <= 0x34) {
			     LED31 = LED31 | TENbot; //add in the TEN hour mask
      }
      else {
        LED32 = LED32 | ELE; //add in the ELEVEN mask
        LED31 = LED31 | VEN;
      }
			break;
		case (0x11):
			//ELEVEN
      if (minutes <= 0x34) {
			     LED32 = LED32 | ELE; //add in the ELEVEN mask
			     LED31 = LED31 | VEN;
      }
      else {
        LED31 = LED31 | TWELVE; //add in the TWELVE mask
      }
			break;
		default:
			//TWELVE
      if (minutes <= 0x34) {
			     LED31 = LED31 | TWELVE; //add in the TWELVE mask
      }
      else {
        LED32 = LED32 | ONE; //add in the ONE mask for second half of hour
      }
	}

	digitalWrite(LEDSlave, HIGH); //Pull up the Chip select pin so we can write to the chip
	SPI.transfer(LED2);  //send the data to the chip
	SPI.transfer(LED41);
	SPI.transfer(LED42);
	SPI.transfer(LED31);
	SPI.transfer(LED32);
	SPI.transfer(LED33);
	digitalWrite(LEDSlave, LOW); //Pull down the chip select pin to latch in the data
	return;  //return whatever the chip has in it
}

//This function controls the change mode.  When change mode is running, the buttons are used to change
//hour, minute, date, month, and year in the RTC chips.  When the top button is pressed and released before
//2s has elapsed, the function cycles between items being changed.  When the top button is pressed and held
//for 2s, all values are written to the RTC and control is returned to the main loop() function
//The bottom button is used to cycle values within each change item
//Default values are pulled from the current RTC values.  Certain defaults will be written to the RTC including
//12 hour mode instead of 24 hour, disabling all alarms and interrupts, and starting the oscillator
//the selection is displayed on the clock face as follows:
//hour mode:
//	the hours are displayed using the hours words as normal
//minute mode:
//	the minutes are only shown when they match the minute interval.  The four button presses corresponding to the
//	intermediate minutes count, but are not displayed
//date:
//	the date is shown 1-9 as hours, then the ten, twenty, and half minute lights are used to count the up to 31 days
//month:
//	the hours are used for month
//year:
//	the program will start at 15 for first initiation and use the ten, twenty, and half lights for additional years
//	up to year thirty nine.
void changeMode() {
	//Add some buttons to track reading for item button
	boolean changeItemButtonReading = false;
	boolean changeItemButtonState = false;
	boolean changeItemButtonLastState = false;
	//add timers to debounce and track hold-downs
	unsigned long changeItemButtonTimer = 0;
	changeModeButtonTimer = millis();
	//Set up a flag to increment the item
	boolean changeItemFlag = false;

	//Set up the variables to hold our clock values.
	byte hour = readClock(RTCHour);
	byte minute = readClock(RTCMinute);
	byte date = readClock(RTCDate);
	byte month = readClock(RTCMonth);
	byte year = readClock(RTCYear);

	//a lonely working int variable for working with byte values to increment correctly
	//then conversion back to BCD bytes for the clock
	unsigned int workingTime = 0;

	//Set up the mode monitor to track what we are changing
	int modeMonitor = 0;

	//Set up the bytes to write to the display as it is being changed
	byte LED33 = 0x00;
	byte LED32 = 0x00;
	byte LED31 = 0x00;
	byte LED42 = 0x00;
	byte LED41 = 0x00;
	byte LED2 = 0x00;


	//Start the change loop.  This only exits when the top button is depressed for 2s and the values are written out
	//to the RTC and control returned to the main loop()
	do {
		//look at the input from the changeItemButton
		//if that button is pressed longer than the debounce timer, evalute the modeMonitor
		//and increment the appropriate counter
		changeItemButtonReading = digitalRead(changeItemButton);
		//reset the changeItemFlag for this read
		changeItemFlag = false;
		//reading is different than we last saw
		if (changeItemButtonReading != changeItemButtonLastState) {
			//reset the timer for debouncing
			changeItemButtonTimer = millis();
		}
		//now check to see if our changeItemButton has passed the debounce timer
		if ((millis() - changeItemButtonTimer) >= 15UL) {
			//update the current state of our button since we've past the debounce timer
			if (changeItemButtonReading != changeItemButtonState) {
				changeItemButtonState = changeItemButtonReading;
				if (changeItemButtonState) {
					changeItemFlag = true;
				}
			}
		}
		//set the last state to the reading we saw
		changeItemButtonLastState = changeItemButtonReading;

		//the changeItemButton was successfully detected as pressed and the flag set
		//depending on the mode, the correct item is incremented here
		if (changeItemFlag) {
			//evaluate what we are changing based upon the mode monitor
			Serial.println("Changing Item!");
			switch (modeMonitor) {
				case 0: //hours
          workingTime = BCDtoInt(hour & 0x1F); //Strip out the AM/PM flag and the AM/PM marker
          if (hour & 0x20) { //is PM hours
            workingTime = workingTime + 12;  //correct for the PM flag having been set
            if (workingTime == 24) {//correct for noon when clock is 12 and PM flag is set
                workingTime = workingTime - 12;
            }
          }
          else { //PM flag not found
            if (workingTime == 12) { //See if it was midnight since time is twelve and PM flag not set
              workingTime = workingTime + 12;
            }
          }

					//increment by one, but with a maximum of 24.  modulo will regulate this.
					//minimum value is 1, not zero
					workingTime = (workingTime % 24) + 1;
					//convert workingTime back to BCD and stick it back into hour.

          //now correct for the AM/PM marker
          if (workingTime > 11) { //set the AM/PM flag and marker back correctly to hour
            if (workingTime == 24) { // set the AM/PM flag correctly for midnight
              hour = 0x40 | InttoBCD(workingTime - 12); //Using AM/PM need hours in 12s
            }
            else {
					    hour = 0x60 | InttoBCD(workingTime - 12);  //Using AM/PM need hours in 12s
            }
          }
          else { //set AM/PM flag back correctly
            hour = 0x40 | InttoBCD(workingTime); //Using AM/PM need hours in 12s (this should do nothing, but just in case)
          }
					break;
				case 1: //minutes
					workingTime = BCDtoInt(minute);
					//increment by one. In this case, minimum is zero, so increment then modulo since we need
					//to roll over after the maximum value of 59.
					workingTime = ((workingTime + 1) % 60);
					//convert workingTime back to BCD and stick it back into minute
					minute = InttoBCD(workingTime);
					break;
				case 2: //date
					workingTime = BCDtoInt(date);
					//since different months have different days, three cases are needed for 28 days, 30 days, and 31 days
					switch (BCDtoInt(month)) {
						case 1:
						case 3:
						case 5:
						case 7:
						case 8:
						case 10:
						case 12:
							//for these months, roll over after 31, minimum value is 1.
							workingTime = (workingTime % 31) + 1;
							break;
						case 2:
							//for february, roll over after 28, no provisions for leap years, sorry
							//minimum value is 1
							workingTime = (workingTime % 28) + 1;
							break;
							//otherwise we assume a 30 days month and roll over after 30
						default:
							//minimum value is 1, max 30.
							workingTime = (workingTime % 30) + 1;
					}
					//convert workingTime back into BCD and stick it into date
					date = InttoBCD(workingTime);
					break;
				case 3: //month
					workingTime = BCDtoInt(month);
					//minimum value 1, roll over after 12
					workingTime = (workingTime % 12) +1;
					//convert workingTime back into BCD and stick it into month
					month = InttoBCD(workingTime);
					break;
				case 4: //year
					workingTime = BCDtoInt(year);
					//increment the year.  This only rolls over after 99 so it'll be a few button presses
					//minimum value is zero
					workingTime = (workingTime + 1) % 100;
					//convert workingTime back into BCD and stick it into year
					 year = InttoBCD(workingTime);
					break;
				default:
					//somehow we got lost, do nothing and reset the modeMonitor
					modeMonitor = 0;
			}
		}

		//look at the input from teh changeModeButton
		//a single depress past the debounce timer changes the modeMonitor
		//a long 2s press
		changeModeButtonReading = digitalRead(changeModeButton);
		if (changeModeButtonReading != changeModeButtonLastState) {
			//if the button has changed state, then reset the debounce timer
			changeModeButtonTimer = millis();
		}
		//check to see if the debounce timer has elapsed
		if ((millis() - changeModeButtonTimer) > 15UL) {
			//debounce timer elapsed, see if we need to update the button State tracker
			if (changeModeButtonState != changeModeButtonReading) {
				changeModeButtonState = changeModeButtonReading;
				//if the button state has changed to low, increment our modeMonitor
				//this means we exceeded our debounce timer, but never exceeded the 2s timer while
				//the button was held down
				if (changeModeButtonState) {
					modeMonitor++;
					//only the 0-4 modes are cycled through, mode 5 exits our change mode
					if (modeMonitor > 4) {
						modeMonitor = 0;
					}
				}
				//button state set because it exceeded the debounce timer
				//now check for 2s hold down to enter change mode
			}
			//button is in the same state as the reading, but we're still past the debounce timer
			else  {
				//measure to make sure we're in the button down state and longer than 2s to exit change mode
				if ((changeModeButtonState) && ((millis() - changeModeButtonTimer) >= 2000UL)) {
					//set the modeMonitor to 5 so the loop will exit
					//also reset the timer so that upon return to the main program, we do not end up right back
					//in the change mode
					changeModeButtonTimer = millis();
					modeMonitor = 5;
				}
			}
		}
		//set the reading to the last state we measured before we loop again
		changeModeButtonLastState = changeModeButtonReading;

		//clear the mask so previous entries aren't retained.
		LED2 = 0x00;
		LED31 = 0x00;
		LED32 = 0x00;
		LED33 = 0x00;
		LED41 = 0x00;
		LED42 = 0x00;

		//display the differing modes in different ways on the clock face
		switch (modeMonitor) {
			case 0://display hours by masking in whatever is in our hour variable to the display LED masks
				switch (hour & 0x1F) { //mask out the AM/PM flags and marker
					case (0x01):
						//ONE
						LED32 = LED32 | ONE; //add in the ONE mask
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x02):
						//TWO
						LED31 = LED31 | TWO; //add in the TWO mask
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x03):
						//THREE
						LED31 = LED31 | THR; //add in the THREE mask
						LED2 = LED2 | EE;
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x04):
						//FOUR
						LED41 = LED41 | FOUR; //add in the FOUR mask
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x05):
						//FIVE
						LED41 = LED41 | FIVEbot; //add in the FIVE hour mask
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x06):
						//SIX
						LED32 = LED32 | SIX; //add in the SIX mask
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x07):
						//SEVEN
						LED32 = LED32 | SEV; //add in the SEVEN mask
						LED2 = LED2 | EN;
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x08):
						//EIGHT
						LED32 = LED32 | EIG; //add in the EIGHT mask
						LED2 = LED2 | HT;
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x09):
						//NINE
						LED41 = LED41 | NINE; //add in the NINE mask
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x10):
						//TEN
						LED31 = LED31 | TENbot; //add in the TEN hour mask
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					case (0x11):
						//ELEVEN
						LED32 = LED32 | ELE; //add in the ELEVEN mask
						LED31 = LED31 | VEN;
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
						break;
					default:
						//TWELVE
						LED31 = LED31 | TWELVE; //add in the TWELVE mask
            //This segment adds in the AM/PM marker using the TO/PAST LEDs
            if ((hour & 0x20) > 0x00) {
              LED41 = LED41 | PAST; //add in the PAST mask for PM
            }
            else {
              LED2 = LED2 | TO; //add in the TO mask for AM
            }
					}
				break;
			case 1://display minutes
				//look for minutes divided into 5 minute groupings
				//we do not show this in to/past form, instead it's adding all the illuminated lights to find the current minute
				//add the minutes mask
				LED41 = LED41 | MINU; //add in the MINUTES mask
				LED33 = LED33 | TES;

				if ((0x00 <= minute) && (minute <= 0x04)) {
				}
				if ((0x05 <= minute) && (minute <= 0x09)) {
					LED41 = LED41 | FIVEtop; //add the FIVE mask
				}
				if ((0x10 <= minute) && (minute <= 0x14)) {
					LED32 = LED32 | TENtop; //add in the TEN mask for minutes
				}
				if ((0x15 <= minute) && (minute <= 0x19)) {
					LED42 = LED42 | AQUA; //add in the AQUARTER mask
					LED41 = LED41 | RTER;
				}
				if ((0x20 <= minute) && (minute <= 0x24)) {
					LED32 = LED32 | TWEtop; //add in the TWENTY mask
					LED31 = LED31 | NTY;
				}
				if ((0x25 <= minute) && (minute <= 0x29)) {
					LED32 = LED32 | TWEtop; //add in the TWENTY mask
					LED31 = LED31 | NTY;
					LED41 = LED41 | FIVEtop; //add the FIVE mask
				}
				if ((0x30 <= minute) && (minute <= 0x34)) {
					LED41 = LED41 | HALF;//add in the HALF mask
				}
				if ((0x35 <= minute) && (minute <= 0x39)) {
					LED41 = LED41 | HALF;//add in the HALF mask
					LED41 = LED41 | FIVEtop; //add the FIVE mask
				}
				if ((0x40 <= minute) && (minute <= 0x44)) {
					LED41 = LED41 | HALF;//add in the HALF mask
					LED32 = LED32 | TENtop; //add in the TEN mask for minutes
				}
				if ((0x45 <= minute) && (minute <= 0x49)) {
					LED41 = LED41 | HALF;//add in the HALF mask
					LED42 = LED42 | AQUA; //add in the AQUARTER mask
					LED41 = LED41 | RTER;
				}
				if ((0x50 <= minute) && (minute <= 0x54)) {
					LED41 = LED41 | HALF;//add in the HALF mask
					LED32 = LED32 | TWEtop; //add in the TWENTY mask
					LED31 = LED31 | NTY;
				}
				if ((0x55 <= minute) && (minute <= 0x59)) {
					LED41 = LED41 | HALF;//add in the HALF mask
					LED32 = LED32 | TWEtop; //add in the TWENTY mask
					LED31 = LED31 | NTY;
					LED41 = LED41 | FIVEtop; //add the FIVE mask
				}

				//now do the 1-4 minute segments between the fives.
				//these are shown using the hours markers since that's what we have
				switch (minute) {
					case (0x01):
						//ONE
						LED32 = LED32 | ONE; //add in the ONE mask
						break;
					case (0x02):
						//TWO
						LED31 = LED31 | TWO; //add in the TWO mask
						break;
					case (0x03):
						//THREE
						LED31 = LED31 | THR; //add in the THREE mask
						LED2 = LED2 | EE;
						break;
					case (0x04):
						//FOUR
						LED41 = LED41 | FOUR; //add in the FOUR mask
						break;
					case (0x06):
						//ONE
						LED32 = LED32 | ONE; //add in the ONE mask
						break;
					case (0x07):
						//TWO
						LED31 = LED31 | TWO; //add in the TWO mask
						break;
					case (0x08):
						//THREE
						LED31 = LED31 | THR; //add in the THREE mask
						LED2 = LED2 | EE;
						break;
					case (0x09):
						//FOUR
						LED41 = LED41 | FOUR; //add in the FOUR mask
						break;
				}

				break;
			case 2://display date
				//this is displayed using the 1-9 hours and the ten, twenty, and half minute illuminators
				//first find the tens section by eliminating the 1-9 item from a compare
				switch (date & 0xF0) {
					case (0x10):
						LED32 = LED32 | TENtop; //add in the TEN mask for minutes
						break;
					case (0x20):
						LED32 = LED32 | TWEtop; //add in the TWENTY mask
						LED31 = LED31 | NTY;
						break;
					case (0x30):
						LED41 = LED41 | HALF;//add in the HALF mask
						break;
				}
				//now find the 1-9 days
				switch (date & 0x0F) {
					case (0x01):
						//ONE
						LED32 = LED32 | ONE; //add in the ONE mask
						break;
					case (0x02):
						//TWO
						LED31 = LED31 | TWO; //add in the TWO mask
						break;
					case (0x03):
						//THREE
						LED31 = LED31 | THR; //add in the THREE mask
						LED2 = LED2 | EE;
						break;
					case (0x04):
						//FOUR
						LED41 = LED41 | FOUR; //add in the FOUR mask
						break;
					case (0x05):
						//FIVE
						LED41 = LED41 | FIVEbot; //add in the FIVE hour mask
						break;
					case (0x06):
						//SIX
						LED32 = LED32 | SIX; //add in the SIX mask
						break;
					case (0x07):
						//SEVEN
						LED32 = LED32 | SEV; //add in the SEVEN mask
						LED2 = LED2 | EN;
						break;
					case (0x08):
						//EIGHT
						LED32 = LED32 | EIG; //add in the EIGHT mask
						LED2 = LED2 | HT;
						break;
					case (0x09):
						//NINE
						LED41 = LED41 | NINE; //add in the NINE mask
						break;
				}
				break;
			case 3://display month
				//this is done just like the hours 1-12
				switch (month) {
					case (0x01):
						//ONE
						LED32 = LED32 | ONE; //add in the ONE mask
						break;
					case (0x02):
						//TWO
						LED31 = LED31 | TWO; //add in the TWO mask
						break;
					case (0x03):
						//THREE
						LED31 = LED31 | THR; //add in the THREE mask
						LED2 = LED2 | EE;
						break;
					case (0x04):
						//FOUR
						LED41 = LED41 | FOUR; //add in the FOUR mask
						break;
					case (0x05):
						//FIVE
						LED41 = LED41 | FIVEbot; //add in the FIVE hour mask
						break;
					case (0x06):
						//SIX
						LED32 = LED32 | SIX; //add in the SIX mask
						break;
					case (0x07):
						//SEVEN
						LED32 = LED32 | SEV; //add in the SEVEN mask
						LED2 = LED2 | EN;
						break;
					case (0x08):
						//EIGHT
						LED32 = LED32 | EIG; //add in the EIGHT mask
						LED2 = LED2 | HT;
						break;
					case (0x09):
						//NINE
						LED41 = LED41 | NINE; //add in the NINE mask
						break;
					case (0x10):
						//TEN
						LED31 = LED31 | TENbot; //add in the TEN hour mask
						break;
					case (0x11):
						//ELEVEN
						LED32 = LED32 | ELE; //add in the ELEVEN mask
						LED31 = LED31 | VEN;
						break;
					default:
						//TWELVE
						LED31 = LED31 | TWELVE; //add in the TWELVE mask
					}
				break;
			case 4://display year
				//this will start at 15 meaning 2015 and go through 59.
				//if this clock lasts 45 years I will probably be as amazed as anyone else
				if ((0x10 <= year) && (year <= 0x19)) {
					LED32 = LED32 | TENtop; //add in the TEN mask for minutes
				}
				if ((0x20 <= year) && (year <= 0x29)) {
					LED32 = LED32 | TWEtop; //add in the TWENTY mask
					LED31 = LED31 | NTY;
				}
				if ((0x30 <= year) && (year <= 0x39)) {
					LED41 = LED41 | HALF;//add in the HALF mask
				}
				if ((0x40 <= year) && (year <= 0x49)) {
					LED41 = LED41 | HALF;//add in the HALF mask
					LED32 = LED32 | TENtop; //add in the TEN mask for minutes
				}
				if ((0x50 <= year) && (year <= 0x59)) {
					LED41 = LED41 | HALF;//add in the HALF mask
					LED32 = LED32 | TWEtop; //add in the TWENTY mask
					LED31 = LED31 | NTY;
				}

				//now do the 1-4 minute segments between the fives.
				//these are shown using the hours markers since that's what we have
				switch (year & 0x0F) {
					case (0x01):
						//ONE
						LED32 = LED32 | ONE; //add in the ONE mask
						break;
					case (0x02):
						//TWO
						LED31 = LED31 | TWO; //add in the TWO mask
						break;
					case (0x03):
						//THREE
						LED31 = LED31 | THR; //add in the THREE mask
						LED2 = LED2 | EE;
						break;
					case (0x04):
						//FOUR
						LED41 = LED41 | FOUR; //add in the FOUR mask
						break;
					case (0x05):
						break;
					case (0x06):
						break;
					case (0x07):
						break;
					case (0x08):
						break;
					case (0x09):
						break;
				}
				break;
			default:
				//modemonitor is either set to 5 for saving or lost.
				//set modemonitor to 5 just in case we got lost.
				modeMonitor = 5;
		}

		//if a change was detected, it should be written out to the display, but only when a change
		//is detected so that a flicker or low level PWM doesn't end up illuminating the wrong
		//things during fast looping
		if (changeItemFlag) {
			//Write out our current display to the clock to show the changes taking place
			digitalWrite(LEDSlave, HIGH); //Pull up the Chip select pin so we can write to the chip
			SPI.transfer(LED2);  //send the data to the chip
			SPI.transfer(LED41);
			SPI.transfer(LED42);
			SPI.transfer(LED31);
			SPI.transfer(LED32);
			SPI.transfer(LED33);
			digitalWrite(LEDSlave, LOW); //Pull down the chip select pin to latch in the data
		}

	} while (modeMonitor < 5);

	//write values to the RTC chip, call the displayTime() function so the user will release the button and return
	Serial.print("Writing: ");
	Serial.print(hour);
	Serial.print(" ");
	Serial.print(minute);
	Serial.print(" ");
	Serial.print(date);
	Serial.print(" ");
	Serial.print(month);
	Serial.print(" ");
	Serial.println(year);
  //clear the clock write protect
  writeClock(0b00001111, 0b00000000); //clear the write protect
  //write out the new time/date to the clock
	writeClock(RTCHour, (hour | 0x40)); //set the hour and make sure 12 hour mode is enabled by setting bit 6 high
	writeClock(RTCMinute, minute);
//	writeClock(RTCDate, 0x03);
//	writeClock(RTCMonth, 0x05);
	writeClock(RTCDate, date);
	writeClock(RTCMonth, month);
	writeClock(RTCYear, year);
  //reenable the clock write protect
  writeClock(0b00001111, 0b01000000); //reenable the write protect
	displayTime();
	return;
}

//compare the date against the programmed birth date to display the RGB color wheel
boolean birthdayCompare() {
	//read the month and date from the RTC
	//return the comparison to the stored values
	return ((readClock(RTCMonth) == birthMonth) && (readClock(RTCDate) == birthDate));
}


//create a colorwheel effect for the birthdate using a theater style crawl.
//a 30 msec delay will be used between color changes however this timer will be run
//in the main loop() program so that a delay will not impact the operation of the clock
//the main loop will also control the master color tracker to keep the colors spinning correctly
void colorWheel(int color) {
  for (int q=0; q < 3; q++) {
    for (uint16_t i=0; i<strip.numPixels(); i=i+3) {
      strip.setPixelColor(i+q, Wheel((color+i) % 255));
    }
    strip.show();
  }
  return;
}

void colorWheelOff() {
  for (uint16_t i=0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, 0);        //turn every third pixel off
  }
  strip.show();
  return;
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
   return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
   WheelPos -= 170;
   return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

//This is the main arduino loop
//This will set timers to count intervals between tasks without locking up the loop using delay()
//By using these counters we can check the clock time approximately every minute and update
//the display.  Since the RTC is keeping real time, this is only the speed up of the update, not
//the actual time.  The actual time should remain accurate.
//This loop also uses the same delay mechanic to debounce and read button inputs to enter time
//change modes and to check the date for the birthday segment display.
void loop() {
	//measure the milliseconds elapsed since board power up and run the function to update the
	//clock display every approximate minute of elapsed time
	if (abs(millis() - rtcReadTimer) >= 60000UL) {
		displayTime();
		birthday = birthdayCompare();
		rtcReadTimer = millis();
	}

//if birthday is active, spin the color wheel on the RBG LEDs
	if (birthday) {
    //change the colors every 30 ms
		if ((millis() - RGBTimer) >= 30UL) {
      //activate the LEDs
      colorWheel(colorTrack);
      //increment the color for the next go around
      colorTrack = (colorTrack + 1) % 255;
      RGBTimer = millis();
		}
	}
	else {
    if (strip.getPixelColor(0)) {
      colorWheelOff();
    }
	}

	//look only for the input from the change mode button.
	//debounce it for 15milliseconds, then start tracking to see if it's held down for 2 seconds
	//to enter the actual mode to change date and time
	//start by reading the change mode button, see if it has changed state
	changeModeButtonReading = digitalRead(changeModeButton);
	if (changeModeButtonReading != changeModeButtonLastState) {
		//if the button has changed state, then reset the debounce timer
		changeModeButtonTimer = millis();
	}
	//check to see if the debounce timer has elapsed
	if ((millis() - changeModeButtonTimer) > 15UL) {
		//debounce timer elapsed, see if we need to update the button State tracker
		if (changeModeButtonState != changeModeButtonReading) {
			changeModeButtonState = changeModeButtonReading;
			//button state set because it exceeded the debounce timer
			//now check for 2s hold down to enter change mode
		}
		//button is in the same state as the reading, but we're still past the debounce timer
		else  {
			//measure to make sure we're in the button down state and longer than 2s to enter change mode
			if ((changeModeButtonState == HIGH) && ((millis() - changeModeButtonTimer) >= 2000UL)) {
				changeModeButtonTimer = millis();
				Serial.println("changeMode found 2s and down");
				changeMode();
			}
		}
	}
	//set the reading to the last state we measured before we loop again
	changeModeButtonLastState = changeModeButtonReading;
}
