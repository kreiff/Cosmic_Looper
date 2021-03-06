/*
  Audio Hacker Library
  Copyright (C) 2013 nootropic design, LLC
  All rights reserved.

  Cosmic Looper code is largely modified and combined example code
  from the NooTropic Design Audio Hacker Project - though some
  refactors are my own design.

  Special thanks to E. Scott Eastman & David Lowenfels for mentorship
  and guidance.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
  A 12-bit sampler to record sampled audio to SRAM.
  The SRAM is divided into 4 equal parts so that 4 samples can be
  recorded.

  This sketch is designed for 8 Arcade Buttons & 6 10k potentiometers, button pinout is:
  Record button = D5
  Sample 0 button = D6
  Sample 1 button = D4
  Sample 2 button = D3
  Sample 3 button = D2
  Reverse button = D1
  Grain Delay button = D0
  Input / Passthrough Mute = Hardwired / Soldered to Input pin of Bypass Switch

  To record a sample, press and hold the record button, then hold a sample button
  for the recording duration.
  Recording threshold has been added to this build. Recording will not start until passthrough signal is above about -50db.
  To play a sample, press and hold the corresponding sample button.
  To play a sample in reverse tap to latch reverse mode, hold for momentary reverse playback.
  To turn on Grain Delay tap the button connected to D0, hold for momentary Grain playback.

  Filter can be turned off / on by pressing the record and reverse buttons simultaneously. This also works in a momentary fashion
  by holding both buttons and then letting go (hold is filter on, let go is filter off).
  
  The Playback buffer can be frozen by holding the Record button during playback. The size of the freeze window
  can be made altered using the Grain Size & Grain Window pots.

  Effect Pot Assignments are:
  Sample Rate: A0
  Bit Crushing: A1
  Low Pass Filter Cutoff: A2
  High Pass Fiter Cutoff: A3
  Grain Delay Window Size: A4
  Grain Size: A5

  Both filter Resonances are static at 150         

  Input is sampled at 16 kHz and reproduced on the output.
  Recordings sampled at 16 kHz and stored to SRAM.
 */

#include <EEPROM.h>
#include <AudioHacker.h>

#define LONG_PRESSED 1
#define HOLDING_PRESS 2
#define SHORT_PRESSED 3
#define LONG_RELEASE 4
#define BUTTON_ON LOW
#define BUTTON_OFF HIGH
#define DEBUG
#define OFF 0
#define PASSTHROUGH 1
#define RECORD 2
#define PLAYBACK 3
#define RECORD_DONE 4
#define RECORD_BUTTON 5
#define SAMPLE0_BUTTON 6
#define SAMPLE1_BUTTON 4
#define SAMPLE2_BUTTON 3
#define SAMPLE3_BUTTON 2
#define REVERSE_BUTTON 1
#define GRAIN_BUTTON 0
#define LOW_PASS 7
#define BAND_PASS 8
#define HIGH_PASS 9

//Playback, Sample Rate, and Sample Address Variables
unsigned int playbackBuf = 2048;
unsigned int passthroughSampleRate;
unsigned int recordingSampleRate;
unsigned int playbackSampleRate;
byte resolution = 12;
unsigned int mask;
unsigned int timer1Start;
volatile unsigned int timer1EndEven;
volatile unsigned int timer1EndOdd;
volatile boolean warning = false;
int currentA0Position;
volatile long address = 0;
volatile long endAddress[4];
volatile byte addressChipNumber = 0;

//Button State Variables
int pins[] = { 6, 4, 3, 2, 5, 1, 0 };
int reading[7];
int buttonState[7] = {BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF};
int lastButtonState[7] = {BUTTON_ON, BUTTON_ON, BUTTON_ON, BUTTON_ON, BUTTON_ON, BUTTON_ON, BUTTON_ON};
int lastShortPressState[7] = {BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF};
int currentShortPressState[7] = {BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF, BUTTON_OFF};
unsigned long lastDebounceTime[7];
unsigned long timeOfLastButtonChange[7];
unsigned long timeSinceLastButtonChange[7];
unsigned long debounceDelay = 20;
unsigned long gateThreshold = 300;

int recordButton = BUTTON_OFF;
int sample0Button = BUTTON_OFF;
int sample1Button = BUTTON_OFF;
int sample2Button = BUTTON_OFF;
int sample3Button = BUTTON_OFF;
int grainButton = BUTTON_OFF;

//Reverse State Variables
int reverseButton = BUTTON_OFF;
volatile int playbackDirection;
volatile long playbackDirectionReset[4];
volatile long playbackDirectionStart[4] = {0, 65535, 0, 65535};

// Filter Variables
int LPfilterCutoff = 255;
int LPfilterResonance = 150;
long LPfeedback;
int LPbuf0 = 0;
int LPbuf1 = 0;

int HPfilterCutoff = 255;
int HPfilterResonance = 150;
long HPfeedback;
int HPbuf0 = 0;
int HPbuf1 = 0;

int filterButton = BUTTON_ON;
int filterState;
int lastFilterState = BUTTON_ON;
int currentShortFilterState = BUTTON_OFF;
int lastShortFilterState = BUTTON_OFF;

//Grain Delay variables
unsigned int nSamplesToPlay = 2048;
volatile unsigned int nSamplesPlayed = 0;
unsigned int grainRead;
unsigned int grainSize;
byte stretch = 1;
volatile long grainAddress = 0;
volatile byte grainAddressChipNumber = 0;

//Freeze Buffer Variables
int freezeButton = BUTTON_OFF;
volatile long freezeAddressReset = 0;
volatile long freezeBuffer;

//Read & Write Buffer & Mode Variables
volatile byte mode = PASSTHROUGH;
unsigned long lastDebugPrint = 0;
unsigned int readBuf[2];
unsigned int writeBuf;
boolean evenCycle = true;

// set to true if you are using a battery backup on the
// SRAM and want to keep sample address info in EEPROM
boolean batteryBackup = true;

unsigned int recordStartTime;
unsigned int recordEndTime;
boolean sampleRecorded[4];
byte sample;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);        // connect to the serial port
#endif
 
  recordingSampleRate = 16000;
  passthroughSampleRate = 16000;
  timer1Start = UINT16_MAX - (F_CPU / passthroughSampleRate);

  pinMode(RECORD_BUTTON, INPUT);
  pinMode(SAMPLE0_BUTTON, INPUT);
  pinMode(SAMPLE1_BUTTON, INPUT);
  pinMode(SAMPLE2_BUTTON, INPUT);
  pinMode(SAMPLE3_BUTTON, INPUT);
  pinMode(REVERSE_BUTTON, INPUT);
  pinMode(GRAIN_BUTTON, INPUT);

  digitalWrite(RECORD_BUTTON, HIGH);
  digitalWrite(SAMPLE0_BUTTON, HIGH);
  digitalWrite(SAMPLE1_BUTTON, HIGH);
  digitalWrite(SAMPLE2_BUTTON, HIGH);
  digitalWrite(SAMPLE3_BUTTON, HIGH);
  digitalWrite(REVERSE_BUTTON, HIGH);
  digitalWrite(GRAIN_BUTTON, HIGH);

  AudioHacker.begin();

#ifdef DEBUG
  Serial.print("sample rate = ");
  Serial.print(passthroughSampleRate);
  Serial.print(" Hz, recording sample rate = ");
  Serial.print(recordingSampleRate);
  Serial.print(" Hz");
  Serial.println();
#endif

  sampleRecorded[0] = false;
  sampleRecorded[1] = false;
  sampleRecorded[2] = false;
  sampleRecorded[3] = false;

  if (batteryBackup) {
    // Read endAddress[] values from EEPROM when we have a battery
    // connected to the Audio Hacker to preserve SRAM contents.
    for (byte i=0;i<4;i++) {
      byte a = i*3;
      long b;
      b = (long)EEPROM.read(a);
      endAddress[i] = (b << 16);
      b = (long)EEPROM.read(a+1);
      endAddress[i] |= (b << 8);
      b = (long)EEPROM.read(a+2);
      endAddress[i] |= b;

      if (endAddress[i] > 0) {
	       sampleRecorded[i] = true;
#ifdef DEBUG
	       Serial.print("sample ");
	       Serial.print(i);
	       Serial.print(" endAddress = ");
	       Serial.print(endAddress[i]);
	       Serial.println();
#endif
      }
    }
  }
}

void loop() {
  //Sample Rate Manipulation
  recordingSampleRate = map(analogRead(0), 0, 1023, 1000, 16000);
  recordingSampleRate = recordingSampleRate - (recordingSampleRate % 100);
  passthroughSampleRate = map(analogRead(0), 0, 1023, 1000, 16000);
  passthroughSampleRate = passthroughSampleRate - (passthroughSampleRate % 100);

  //Button State Check
  for(int i = 0; i<7; i++) {
  reading[i] = digitalRead(pins[i]);
  }
  
  //Loop through to set states for Sample and Record Buttons
  for (int j = 0; j<5; j++) {

  // check to see if you just pressed the button
  // (i.e. the input went from LOW to HIGH), and you've waited long enough
  // since the last press to ignore any noise:

  // If the switch changed, due to noise or pressing:
  if (reading[j] != lastButtonState[j]) {
    // reset the debouncing timer
    lastDebounceTime[j] = millis();
  }

  if ((millis() - lastDebounceTime[j]) > debounceDelay) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading[j] != buttonState[j]) {
        buttonState[j] = reading[j];
        if (j == 4){
          freezeAddressReset = address;
        }
    }
  }

  // set the button states:
  if (j == 0){
     sample0Button = buttonState[j];
  }
  if (j == 1){
     sample1Button = buttonState[j];
  }
  if (j == 2){
     sample2Button = buttonState[j];
  }
  if (j == 3){
     sample3Button = buttonState[j];
  }  
  if (j == 4){
     recordButton = buttonState[j];
  }
  // save the reading. Next time through the loop, it'll be the lastButtonState:
  lastButtonState[j] = reading[j];
  }

   //Filter State Check
  if (reading[4] != lastButtonState[5]) { // if pin state has changed from the last reading
    timeOfLastButtonChange[4] = millis();
  }
  timeSinceLastButtonChange[4] = millis() - timeOfLastButtonChange[4];
  if (reading[5] != lastButtonState[5]) { // if pin state has changed from the last reading
    timeOfLastButtonChange[5] = millis();
  }
  timeSinceLastButtonChange[5] = millis() - timeOfLastButtonChange[5];

  if ((reading[4] == BUTTON_ON && reading[5] == BUTTON_ON) && (timeSinceLastButtonChange[4] < debounceDelay) && (timeSinceLastButtonChange[5] < debounceDelay)) {
    // has only been low for less than the debounce time - do nothing
  }
   if ((reading[4] == BUTTON_ON && timeSinceLastButtonChange[4] > gateThreshold) && (reading[5] == BUTTON_ON && timeSinceLastButtonChange[5] > gateThreshold) && filterState != LONG_PRESSED) {
      filterState = LONG_PRESSED;
      filterButton = BUTTON_ON;   
  }
  if ((reading[4] == BUTTON_ON && timeSinceLastButtonChange[4] > debounceDelay && timeSinceLastButtonChange[4] < gateThreshold) && (reading[5] == BUTTON_ON && timeSinceLastButtonChange[5] > debounceDelay && timeSinceLastButtonChange[5] < gateThreshold) && filterState != HOLDING_PRESS) {
      filterState = HOLDING_PRESS;
      filterButton = BUTTON_ON;  
  }
  if ((reading[4] == BUTTON_OFF && timeSinceLastButtonChange[4] > debounceDelay) && (reading[5] == BUTTON_OFF && timeSinceLastButtonChange[5] > debounceDelay) && filterState == HOLDING_PRESS) {
      filterState = SHORT_PRESSED;
      if (currentShortFilterState == BUTTON_OFF && lastShortFilterState == BUTTON_ON){  
      if (filterButton == BUTTON_OFF){ 
      filterButton = BUTTON_ON;
      }
      else{
      filterButton = BUTTON_OFF;
      }
      }
  lastShortFilterState = filterButton; 
  }
  
  if ((reading[4] == BUTTON_OFF && timeSinceLastButtonChange[4] > debounceDelay) && (reading[5] == BUTTON_OFF && timeSinceLastButtonChange[5] > debounceDelay) && filterState == LONG_PRESSED) {
      filterState = LONG_RELEASE;
      filterButton = BUTTON_OFF;   
  }
  lastButtonState[4] = reading[4];
  lastButtonState[5] = reading[5];
 
 // Loop through to set latching or momentary states for Reverse and Grain Delay Buttons
 for (int k = 5; k<7; k++) {

  if (reading[k] != lastButtonState[k]) { // if pin state has changed from the last reading
    timeOfLastButtonChange[k] = millis(); // if pin state different, store the time of the state change
  }
  timeSinceLastButtonChange[k] = millis() - timeOfLastButtonChange[k];

  if (reading[k] == BUTTON_ON && timeSinceLastButtonChange[k] < debounceDelay) {
    // has only been low for less than the debounce time - do nothing
  }
  if (reading[k] == BUTTON_ON && reading[4] == BUTTON_OFF && timeSinceLastButtonChange[k] > gateThreshold && buttonState[k] != LONG_PRESSED) {
      buttonState[k] = LONG_PRESSED;
      if (k == 5){
      reverseButton = BUTTON_ON;  
      }
      if (k == 6){
      grainButton = BUTTON_ON; 
      }
  }
  if (reading[k] == BUTTON_ON && reading[4] == BUTTON_OFF && timeSinceLastButtonChange[k] > debounceDelay && timeSinceLastButtonChange[k] < gateThreshold && buttonState[k] != HOLDING_PRESS) {
      buttonState[k] = HOLDING_PRESS;
      if (k == 5){
      reverseButton = BUTTON_ON;  
      }
      if (k == 6){
      grainButton = BUTTON_ON; 
      }
  }
  if (reading[k] == BUTTON_OFF && timeSinceLastButtonChange[k] > debounceDelay && buttonState[k] == HOLDING_PRESS) {
      buttonState[k] = SHORT_PRESSED;
      if (currentShortPressState[k] == BUTTON_OFF && lastShortPressState[k] == BUTTON_ON){
      if (k == 5){  
      if (reverseButton == BUTTON_OFF) 
      reverseButton = BUTTON_ON;
      else
      reverseButton = BUTTON_OFF;
      }
      if (k == 6){  
      if (grainButton == BUTTON_OFF) 
      grainButton = BUTTON_ON;
      else
      grainButton = BUTTON_OFF;
      }
  }
  if (k == 5){ 
  lastShortPressState[k] = reverseButton;
  }
  if (k == 6){ 
  lastShortPressState[k] = grainButton;
  }
  }
  if (reading[k] == BUTTON_OFF && timeSinceLastButtonChange[k] > debounceDelay && buttonState[k] == LONG_PRESSED) {
      buttonState[k] = LONG_RELEASE;
      if (k == 5){
      reverseButton = BUTTON_OFF;  
      }
      if (k == 6){
      grainButton = BUTTON_OFF; 
      }   
  }
  lastButtonState[k] = reading[k];
  }
 
  //Bit Crushing Pot Read
  resolution = map(analogRead(1), 0, 1023, 1, 12);
  mask = 0x0FFF << (12-resolution);

  // Dual Filters Pot State Read - Cutoff only, Resonance is static
  LPfilterCutoff = analogRead(2) >> 2;
  HPfilterCutoff = analogRead(3) >> 2;

  LPfeedback = (long)LPfilterResonance + (long)(((long)LPfilterResonance * ((int)255 - (255-LPfilterCutoff))) >> 8);
  HPfeedback = (long)HPfilterResonance + (long)(((long)HPfilterResonance * ((int)255 - (255-HPfilterCutoff))) >> 8);

#ifdef DEBUG
  if ((millis() - lastDebugPrint) >= 1000) {
    lastDebugPrint = millis();

    // Print the number of instruction cycles remaining at the end of the ISR.
    // The more work you try to do in the ISR, the lower this number will become.
    // If the number of cycles remaining reaches 0, then the ISR will take up
    // all the CPU time and the code in loop() will not run.

    /*
    Serial.print("even cycles remaining = ");
    Serial.print(UINT16_MAX - timer1EndEven);
    Serial.print("   odd cycles remaining = ");
    Serial.print(UINT16_MAX - timer1EndOdd);
    Serial.println();
    if (((UINT16_MAX - timer1EndEven) < 20) || (((UINT16_MAX - timer1EndOdd) < 20))) {
      Serial.println("WARNING: ISR execution time is too long. Reduce sample rate or reduce the amount of code in the ISR.");
    }
    */
  }
#endif

  if ((mode == OFF) || (mode == PASSTHROUGH)) {
    if ((recordButton == BUTTON_ON) && ((sample0Button == BUTTON_ON) || (sample1Button == BUTTON_ON) || (sample2Button == BUTTON_ON) || (sample3Button == BUTTON_ON))) {
      // enter RECORD mode
      recordStartTime = millis();
      if (((recordStartTime - recordEndTime) < 20) || (playbackBuf < 2100)) {
        // debounce the record button.
        recordStartTime = 0;
        return;
      }
      if (sample0Button == BUTTON_ON) {
        sample = 0;
        address = 0;
        addressChipNumber = 0;
      }
      if (sample1Button == BUTTON_ON) {
        sample = 1;
        address = 65535;
        addressChipNumber = 0;
      }
      if (sample2Button == BUTTON_ON) {
        sample = 2;
        address = 0;
        addressChipNumber = 1;
      }
      if (sample3Button == BUTTON_ON) {
        sample = 3;
        address = 65535;
        addressChipNumber = 1;
      }
      mode = RECORD;
      timer1Start = UINT16_MAX - (F_CPU / recordingSampleRate);
      currentA0Position = analogRead(0);
    } else {
      // enter PLAYBACK mode
      if ((sample0Button == BUTTON_ON) && (sampleRecorded[0])) {
        address = playbackDirectionStart[0];
        addressChipNumber = 0;
        sample = 0;
        mode = PLAYBACK;
        grainAddress = playbackDirectionStart[sample];
        grainAddressChipNumber = 0;
        nSamplesPlayed = 0;
      }
      if ((sample1Button == BUTTON_ON) && (sampleRecorded[1])) {
        address = playbackDirectionStart[1];
        addressChipNumber = 0;
        sample = 1;
        mode = PLAYBACK;
        grainAddress = playbackDirectionStart[sample];
        grainAddressChipNumber = 0;
        nSamplesPlayed = 0;
      }
      if ((sample2Button == BUTTON_ON) && (sampleRecorded[2])) {
        address = playbackDirectionStart[2];
        addressChipNumber = 1;
        sample = 2;
        mode = PLAYBACK;
        grainAddress = playbackDirectionStart[sample];
        grainAddressChipNumber = 1;
        nSamplesPlayed = 0;
      }
      if ((sample3Button == BUTTON_ON) && (sampleRecorded[3])) {
        address = playbackDirectionStart[3];
        addressChipNumber = 1;
        sample = 3;
        mode = PLAYBACK;
        grainAddress = playbackDirectionStart[sample];
        grainAddressChipNumber = 1;
        nSamplesPlayed = 0;
      }
    }
  }

  if (mode == PASSTHROUGH) {
    timer1Start = UINT16_MAX - (F_CPU / passthroughSampleRate);
  }

  if (mode == RECORD) {
    if (((sample == 0) && (sample0Button == BUTTON_OFF)) ||
        ((sample == 1) && (sample1Button == BUTTON_OFF)) ||
        ((sample == 2) && (sample2Button == BUTTON_OFF)) ||
        ((sample == 3) && (sample3Button == BUTTON_OFF))) {
          // recording stopped
          recordEndTime = millis();
          if (recordEndTime - recordStartTime < 20) {
            // debounce
            return;
          }
          sampleRecorded[sample] = true;
          endAddress[sample] = address;
#ifdef DEBUG
          Serial.print("sample ");
          Serial.print(sample);
          Serial.print(" recording time = ");
          Serial.print(recordEndTime - recordStartTime);
          Serial.println(" ms");
          Serial.print(" endAddress = ");
          Serial.println(endAddress[sample]);
#endif

          if (batteryBackup) {
            // Write endAddress to EEPROM for battery backup use.
            byte a = sample*3;
            EEPROM.write(a, (endAddress[sample] >> 16) & 0xFF);
            EEPROM.write(a+1, (endAddress[sample] >> 8) & 0xFF);
            EEPROM.write(a+2, endAddress[sample] & 0xFF);
          }
          mode = PASSTHROUGH;
        }
  } else {
  }

  if (mode == RECORD_DONE) {
    if (recordStartTime != 0) {
#ifdef DEBUG
      Serial.print("sample ");
      Serial.print(sample);
      Serial.print(" recording time = ");
      Serial.print(millis() - recordStartTime);
      Serial.println(" ms");
      Serial.print(" endAddress = ");
      Serial.println(endAddress[sample]);
#endif
      sampleRecorded[sample] = true;
      recordStartTime = 0;

      if (batteryBackup) {
        // Write endAddress to EEPROM for battery backup use.
        byte a = sample*3;
        EEPROM.write(a, (endAddress[sample] >> 16) & 0xFF);
        EEPROM.write(a+1, (endAddress[sample] >> 8) & 0xFF);
        EEPROM.write(a+2, endAddress[sample] & 0xFF);
      }
    }
    if (recordButton == BUTTON_OFF) {
      // record button released
      mode = PASSTHROUGH;
    }
  }

  if (mode == PLAYBACK) {
    //Freeze Buffer On / Off
    if (recordButton == BUTTON_ON && reading[5]!= BUTTON_ON){
    freezeButton = BUTTON_ON;
    }else{
    freezeButton = BUTTON_OFF;
    }
    
    if (((sample == 0) && (sample0Button == BUTTON_OFF)) ||
        ((sample == 1) && (sample1Button == BUTTON_OFF)) ||
        ((sample == 2) && (sample2Button == BUTTON_OFF)) ||
        ((sample == 3) && (sample3Button == BUTTON_OFF))) {
          // play button released
          mode = PASSTHROUGH;
    } else {
      grainRead = map(analogRead(4), 0, 1023, 3, 682);
      grainSize = grainRead*3;
      stretch = map(analogRead(5), 0, 1023, 1, 20);
      playbackSampleRate = map(analogRead(0), 0, 1023, 1000, 16000);
      // nSamplesToPlay is the number of samples to play from each grain.
      nSamplesToPlay = stretch * grainSize * ((float)playbackSampleRate/(float)recordingSampleRate);
      // compute the start value for counter1 to achieve the chosen playback rate
      timer1Start = UINT16_MAX - (F_CPU / playbackSampleRate);
    }
  }
 //Play Direction Address Update 
 if (reverseButton == BUTTON_ON){
     playbackDirection = -3;
     
     playbackDirectionReset[0] = 0;
     playbackDirectionReset[1] = 65535;
     playbackDirectionReset[2] = 0;
     playbackDirectionReset[3] = 65535;
    
     playbackDirectionStart[0] = endAddress[0] - 3;
     playbackDirectionStart[1] = endAddress[1] - 3;
     playbackDirectionStart[2] = endAddress[2] - 3;
     playbackDirectionStart[3] = endAddress[3] - 3;
     }else{
     playbackDirection = 3;
     
     playbackDirectionReset[0] = endAddress[0];
     playbackDirectionReset[1] = endAddress[1];
     playbackDirectionReset[2] = endAddress[2];
     playbackDirectionReset[3] = endAddress[3];

     playbackDirectionStart[0] = 0;
     playbackDirectionStart[1] = 65535;
     playbackDirectionStart[2] = 0;
     playbackDirectionStart[3] = 65535;
     }   
}

ISR(TIMER1_OVF_vect) {
  TCNT1 = timer1Start;
  int mix;
  unsigned int signal;

  if (mode != RECORD_DONE) {
    AudioHacker.writeDAC(playbackBuf);
  }

  if ((mode != PLAYBACK) && (mode != RECORD_DONE)) {
    // Read ADC
    signal = AudioHacker.readADC(); 
    mix = signal - 2048;
     
  }

  if (mode == RECORD) {
    if (evenCycle) {
      // we only write to memory on odd cycles, so buffer the sampled signal.
      writeBuf = signal;
    } else {
      // Write to SRAM
      AudioHacker.writeSRAMPacked(addressChipNumber, address, writeBuf, signal);

      address += 3;
      if (((sample == 0) && (address > 65532)) ||
          ((sample == 1) && (address > MAX_ADDR)) ||
          ((sample == 2) && (address > 65532)) ||
          ((sample == 3) && (address > MAX_ADDR))) {
            // end of memory, stop recording
            mode = RECORD_DONE;
            endAddress[sample] = address;
      }
    }
  }


  if (mode == PLAYBACK) {
    if (evenCycle) {
      // Read from SRAM
      unsigned int passbuff = AudioHacker.readADC();
      AudioHacker.readSRAMPacked(addressChipNumber, address, readBuf);
      signal = readBuf[0];
      //mixing passthrough buffer with sample signal
      mix = (passbuff - 2048) + (signal - 2048);

 //Grain delay interupt implementation     
      if (grainButton == BUTTON_ON){
      nSamplesPlayed+=3;
      if (nSamplesPlayed >= nSamplesToPlay) {
      // proceed to the next grain
      nSamplesPlayed = 0;
      if (reverseButton == BUTTON_ON){
      grainAddress -= grainSize;
      if(sample == 0 || sample == 2){
       if (grainAddress <= 0) {
        grainAddress = endAddress[sample] - 3;
      } 
      }
      if(sample == 1 || sample == 3){
       if (grainAddress < 65535) {
        grainAddress = endAddress[sample] - 3;
      } 
      } 
      }else{
      grainAddress += grainSize;  
      if (grainAddress > endAddress[sample]) {
        grainAddress = playbackDirectionStart[sample];
      }
      }
      address = grainAddress;
      mix = signal;
      return;
    }
 }

  //Freeze Mode interupt implementation     
      if (freezeButton == BUTTON_ON){
       nSamplesPlayed+=3;
      if (nSamplesPlayed >= nSamplesToPlay) {
      // proceed to the next grain
      nSamplesPlayed = 0;
      if (reverseButton == BUTTON_ON){
      grainAddress -= grainSize;
      if(sample == 0 || sample == 2){
       if (grainAddress >= 0) {
        grainAddress = freezeAddressReset;
      } 
      }
      if(sample == 1 || sample == 3){
       if (grainAddress > 65535) {
        grainAddress = freezeAddressReset;
      } 
      } 
      }else{
      grainAddress += grainSize;  
      if (grainAddress < endAddress[sample]) {
        grainAddress = freezeAddressReset;
      }
      }
      address = grainAddress;
      mix = signal;
      return;
    }
 }
 
     //prevents playback clipping when mixing passthrough with samples
      if (mix < -2048) {
      mix = -2048;
    } else {
      if (mix > 2047) {
        mix = 2047;
      }
    }
      
      address += playbackDirection;
      if ((sample == 0) && (address == playbackDirectionReset[0])) {
        address = playbackDirectionStart[0];
        addressChipNumber = 0;
      }
      if ((sample == 1) && (address == playbackDirectionReset[1])) {
        address = playbackDirectionStart[1];
        addressChipNumber = 0;
      }
      if ((sample == 2) && (address == playbackDirectionReset[2])) {
        address = playbackDirectionStart[2];
        addressChipNumber = 1;
      }
      if ((sample == 3) && (address == playbackDirectionReset[3])) {
        address = playbackDirectionStart[3];
        addressChipNumber = 1;
      }
    } else {
      unsigned int passbuff = AudioHacker.readADC();
      signal = readBuf[1];
      mix = (passbuff - 2048) + (signal - 2048);

      //Prevents Playback Clipping
      if (mix < -2048) {
      mix = -2048;
    } else {
      if (mix > 2047) {
        mix = 2047;
      }
     }    
    }
   } // PLAYBACK
  
  //Interupt Filter Implementation
  if (filterButton == BUTTON_ON){
  int highPass1 = mix - LPbuf0;
  int bandPass1 = LPbuf0 - LPbuf1;
  int tmp1 = highPass1 + (LPfeedback * bandPass1 >> 8);
  LPbuf0 += ((long)LPfilterCutoff * tmp1) >> 8;
  LPbuf1 += ((long)LPfilterCutoff * (LPbuf0 - LPbuf1)) >> 8;
  mix = LPbuf1;

  int highPass2 = mix - HPbuf0;
  int bandPass2 = HPbuf0 - HPbuf1;
  int tmp2 = highPass2 + (HPfeedback * bandPass2 >> 8);
  HPbuf0 += ((long)HPfilterCutoff * tmp2) >> 8;
  HPbuf1 += ((long)HPfilterCutoff * (HPbuf0 - HPbuf1)) >> 8;
  mix = highPass2 + 2048;
  }
  else{
  mix = mix + 2048;  
  }
 
  playbackBuf = mix;
  //Interrupt bit crush implementation (Move before filter for filtered distortion)
  playbackBuf &= mask;

#ifdef DEBUG
  if (evenCycle) {
    timer1EndEven = TCNT1;
  } else {
    timer1EndOdd = TCNT1;
  }
#endif
  evenCycle = !evenCycle;
}
