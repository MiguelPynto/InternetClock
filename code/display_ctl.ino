#include <Wire.h>
#include <ShiftDisplay.h> // https://github.com/MiguelPynto/ShiftDisplay

// SDA_PIN = A4; atmega328 pin 27
// SCL_PIN = A5; atmega328 pin 28
// LATCH_PIN = 6; atmega328 pin 12
// CLOCK_PIN = 7; atmega328 pin 13
// DATA_PIN = 5; atmega328 pin 11
const int COLON_PIN = 10; // atmega328 pin 16
const int LED_PIN = 9; // atmega328 pin 15

const int MY_ADDRESS = 56;
const int TIMEOUT = 2000; // milliseconds
const byte ERROR[] = {B10001100, B10000000, B10000000, B11100000, B00011100, B00010000, B00010000, B01110000};
const int DISPLAY_TYPE = COMMON_CATHODE;
const int DISPLAY_SIZE = 8;

ShiftDisplay display(DISPLAY_TYPE, DISPLAY_SIZE);

volatile bool colon;
volatile unsigned long colonStart;
volatile unsigned long willTimeout;

void clear() {
	display.set(ERROR);
	digitalWrite(LED_PIN, LOW);
	digitalWrite(COLON_PIN, LOW);
}

void msgEvent(int size) {

	if (size == 1) { // colon msg
		colon = Wire.read();
		if (colon)
			colonStart = millis();
		else
			digitalWrite(COLON_PIN, LOW);
	}

	else if (size == 2) { // led msg
		bool status = Wire.read();
		Wire.read(); // discard
		digitalWrite(LED_PIN, status);
	}

	else { // display msg
		char chars[8];
		bool dots[8];
		for (int i = 0; i < 8; i++)
			chars[i] = Wire.read();
		for (int i = 0; i < 8; i++)
			dots[i] = Wire.read();
		display.set(chars, dots);
	}
	
	willTimeout = millis() + TIMEOUT;
}

void setup() {
	pinMode(LED_PIN, OUTPUT);
	pinMode(COLON_PIN, OUTPUT);
	clear();
	Wire.begin(MY_ADDRESS);
	Wire.onReceive(msgEvent);
	willTimeout = TIMEOUT;
}

void loop() {

	if (millis() > willTimeout) // is dead
		clear();

	if (colon) {
		int x = (millis() - colonStart) / 50; // 0s,x=0  0.5s,x=10  1s,x=20  2s,x=40  ...
		int y = abs(abs((x%20)-10)-10); // x=0,y=0  x=10,y=10  x=20,y=0  x=30,y=10  ...
		analogWrite(COLON_PIN, y);
	}

	display.show();
}
