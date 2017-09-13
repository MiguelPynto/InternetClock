#include <Wire.h>
#include <DHT.h> // https://github.com/markruys/arduino-DHT
#include <PinChangeInterrupt.h> // https://github.com/NicoHood/PinChangeInterrupt
// core https://github.com/SpenceKonde/ATTinyCore

const int DISPLAY_CTL_ADDRESS = 56;
const int DISPLAY_ID = 1;
const int PACKET_SIZE = 10;
// SDA = 0; attiny85 pin 5
// SCL = 2; attiny85 pin 7

const int DHT_PIN = 3; // attiny85 pin 2
const int DHT_MIN_TEMP = -40;
const int DHT_MAX_TEMP = 125;
const int DHT_MIN_HUM = 0;
const int DHT_MAX_HUM = 100;
const int MAX_ERROR = 3;

const char TEMP_SYMBOL = 'c';
const char MIN_TEMP_SYMBOL = 'n';
const char MAX_TEMP_SYMBOL = 'm';
const char HUM_SYMBOL = 'u';
const char MIN_HUM_SYMBOL = 'n';
const char MAX_HUM_SYMBOL = 'm';
//const byte MAX_TEMP_SYMBOL = B10011010; //TODO

typedef enum { // modes
	LOOP,
	CURR_TEMP,
	MIN_TEMP,
	MAX_TEMP,
	CURR_HUM,
	MIN_HUM,
	MAX_HUM,
	MODES_COUNT
};
const int DEFAULT_MODE = LOOP;
const int BUTTON_MODE_PIN = 4; // attiny85 pin 3
const int BUTTON_CLEAR_PIN = 1; // attiny85 pin 6
const int DEBOUNCE_TIME = 50;

volatile bool buttonModePressed;
volatile bool buttonClearPressed;
DHT dht;

struct measurements_t {
	int currTemp;
	int minTemp;
	int maxTemp;
	int currHum;
	int minHum;
	int maxHum;
};

/*int loadMode() {
	int mode;
	EEPROM.get(MEM_ADRESS, mode);
	if (mode < 0 || mode >= MODES_COUNT) // first run
		mode = DEFAULT_MODE;
	return mode;
}*/

/*void saveMode(int mode) {
	EEPROM.put(MEM_ADRESS, mode);
}*/

void formatNumber(byte packet[], int n, char symbol) {
	if (n >= 1000) {
		packet[1] = '0' + (n / 1000);
		packet[2] = '0' + (n / 100 % 10);
		packet[3] = '0' + (n / 10 % 10);
	} else if (n >= 100) {
		packet[1] = '0' + (n / 100);
		packet[2] = '0' + (n / 10 % 10);
		packet[3] = '0' + (n % 10);
		packet[6] = true;
	} else if (n >= 0) {
		packet[2] = '0' + (n / 10);
		packet[3] = '0' + (n % 10);
		packet[6] = true;
	} else if (n <= -100) {
		n = -n;
		packet[1] = '-';
		packet[2] = '0' + (n / 100);
		packet[3] = '0' + (n / 10 % 10);
	} else { // n < 0
		n = -n;
		packet[1] = '-';
		packet[2] = '0' + (n / 10);
		packet[3] = '0' + (n % 10);
		packet[6] = true;
	}
	packet[4] = symbol;
}

void formatInteger(byte packet[], int n, char symbol) {
	if (n >= 1000) {
		packet[1] = '0' + (n / 1000);
		packet[2] = '0' + (n / 100 % 10);
		packet[3] = '0' + (n / 10 % 10);
	} else if (n >= 100) {
		packet[2] = '0' + (n / 100);
		packet[3] = '0' + (n / 10 % 10);
	} else if (n >= 0) {
		packet[3] = '0' + (n / 10);
	} else if (n <= -100) {
		n = -n;
		packet[1] = '-';
		packet[2] = '0' + (n / 100);
		packet[3] = '0' + (n / 10 % 10);
	} else { // n < 0
		n = -n;
		packet[2] = '-';
		packet[3] = '0' + (n / 10);
	}
	packet[4] = symbol;
}

// send to display
void display(int mode, measurements_t m) {
	byte packet[PACKET_SIZE];

	// initialize packet
	packet[0] = DISPLAY_ID; // which display will print
	packet[1] = packet[2] = packet[3] = packet[4] = ' '; // display characters
	packet[5] = packet[6] = packet[7] = packet[8] = false; // dot for each character
	packet[9] = false; // colon blink

	// format packet for current mode
	switch (mode) {
		case LOOP: // changes every 10 seconds
			if (millis() / 10000 % 2)
				formatInteger(packet, m.currHum, HUM_SYMBOL);
			else
				formatInteger(packet, m.currTemp, TEMP_SYMBOL);
			break;
		case CURR_TEMP:
			formatNumber(packet, m.currTemp, TEMP_SYMBOL); break;
		case MIN_TEMP:
			formatNumber(packet, m.minTemp, MIN_TEMP_SYMBOL); break;
		case MAX_TEMP:
			formatNumber(packet, m.maxTemp, MAX_TEMP_SYMBOL); break;
		case CURR_HUM:
			formatNumber(packet, m.currHum, HUM_SYMBOL); break;
		case MIN_HUM:
			formatNumber(packet, m.minHum, MIN_HUM_SYMBOL); break;
		case MAX_HUM:
			formatNumber(packet, m.maxHum, MAX_HUM_SYMBOL); break;
	}

	// send packet
	Wire.beginTransmission(DISPLAY_CTL_ADDRESS);
	Wire.write(packet, PACKET_SIZE);
	Wire.endTransmission();
}

void buttonClearInt() {
	static unsigned long buttonClearTime = 0;
	uint8_t trigger = getPinChangeInterruptTrigger(digitalPinToPCINT(BUTTON_CLEAR_PIN));
	if (trigger == FALLING) { // button pressed
		if (millis() - buttonClearTime > DEBOUNCE_TIME) // debounce
			buttonClearPressed = true;
	} else // button released
		buttonClearTime = millis();
}

void buttonModeInt() {
	static unsigned long buttonModeTime = 0;
	uint8_t trigger = getPinChangeInterruptTrigger(digitalPinToPCINT(BUTTON_MODE_PIN));
	if (trigger == FALLING) { // button pressed
		if (millis() - buttonModeTime > DEBOUNCE_TIME) // debounce
			buttonModePressed = true;
	} else // button released
		buttonModeTime = millis();
}

void setup() {
	dht.setup(DHT_PIN, DHT::DHT22);
	Wire.begin();
	buttonModePressed = buttonClearPressed = false;
	pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
	pinMode(BUTTON_CLEAR_PIN, INPUT_PULLUP);
	attachPinChangeInterrupt(digitalPinToPCINT(BUTTON_MODE_PIN), buttonModeInt, CHANGE);
	attachPinChangeInterrupt(digitalPinToPCINT(BUTTON_CLEAR_PIN), buttonClearInt, CHANGE);
}

void loop() {
	static int mode = DEFAULT_MODE;
	static unsigned long prevReading = 0;
	static int errorCount = MAX_ERROR + 1; // init printing error if first reads fail
	static measurements_t m;

	if (millis() - prevReading > dht.getMinimumSamplingPeriod()) { // sensor has new data

		// read data
		float temp = dht.getTemperature();
		float hum = dht.getHumidity();
		prevReading = millis();

		// save data
		if (dht.getStatus() != DHT::ERROR_NONE) { // sensor data error
			errorCount++;
		} else { // sensor data ok
			errorCount = 0;
			m.currTemp = (int)((float)(temp*10)); // ex 12.3 = 123
			m.currHum = (int)((float)(hum*10));
			if (m.minTemp == NULL || m.currTemp < m.minTemp)
				m.minTemp = m.currTemp;
			if (m.maxTemp == NULL || m.currTemp > m.maxTemp)
				m.maxTemp = m.currTemp;
			if (m.minHum == NULL || m.currHum < m.minHum)
				m.minHum = m.currHum;
			if (m.maxHum == NULL || m.currHum > m.maxHum)
				m.maxHum = m.currHum;
		}

		// print data
		if (errorCount <= MAX_ERROR)
			display(mode, m);
	}

	if (buttonModePressed) {
		if (errorCount <= MAX_ERROR) { // only change mode if screen has data
			mode = (mode + 1) % MODES_COUNT; // iterate mode
			display(mode, m);
		}
		buttonModePressed = false;
	}
	
	if (buttonClearPressed) {
		if (errorCount <= MAX_ERROR) { // only clear if screen has data
			m.minTemp = m.maxTemp = m.currTemp;
			m.minHum = m.maxHum = m.currHum;
			display(mode, m);
		}
		buttonClearPressed = false;
	}
}
