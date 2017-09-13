#include <Wire.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <TimeLib.h> // https://github.com/PaulStoffregen/Time
#include <Timezone.h> // https://github.com/JChristensen/Timezone
// core https://github.com/esp8266/Arduino

const int MEMORY_SIZE = 512;
const int MEMORY_ADDRESS_A = 0;
const int MEMORY_ADDRESS_B = 4;

const int SDA_PIN = 1;
const int SCL_PIN = 3;
const int BUTTON_A_PIN = 2;
const int BUTTON_B_PIN = 0;
const int DISPLAY_CTL_ADDRESS = 56;

const char WIFI_SSID[] = "********";
const char WIFI_KEY[] = "********";
const char HOSTNAME[] = "sd-clock";
const int LOCAL_PORT = 8888;
const char NTP_SERVER[] = "pt.pool.ntp.org";
const int NTP_PORT = 123;
const int NTP_PACKET_SIZE = 48;

const int SYNC_HOUR = 5; // utc hour
const int WIFI_TIMEOUT = 30; // seconds
const int NTP_TIMEOUT = 8000; // milliseconds
const int SYNC_TIMEOUT = 5; // tries
const int DEBOUNCE_TIME = 50;

const TimeChangeRule BST = {"BST", Last, Sun, Mar, 1, 60};
const TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 2, 0};
Timezone pt(BST, GMT); // portugal

enum Mode {
	CLOCK_24_TRAILZERO,
	CLOCK_24,
	CLOCK_12_TRAILZERO,
	CLOCK_12,
	HOUR_24_TRAILZERO,
	HOUR_24,
	HOUR_12_TRAILZERO,
	HOUR_12,
	MINUTE_TRAILZERO,
	MINUTE,
	SECOND_TRAILZERO,
	SECOND,
	AM_PM,
	AM_PM_DOTLESS,
	DATE_TRAILZERO,
	DATE,
	DATE_NAME_TRAILZERO,
	DATE_NAME,
	DAY_TRAILZERO,
	DAY,
	MONTH_TRAILZERO,
	MONTH,
	MONTH_NAME,
	YEAR,
	WEEKDAY,
	DATE_LOOP,
	EMPTY,
	MODES_COUNT
};
const Mode DEFAULT_MODE_A = CLOCK_24;
const Mode DEFAULT_MODE_B = DATE_LOOP;

volatile bool buttonAPressed, buttonBPressed;
volatile unsigned long buttonATime, buttonBTime;
bool syncing;

Mode loadModeA() {
	Mode mode;
	EEPROM.begin(MEMORY_SIZE);
	EEPROM.get(MEMORY_ADDRESS_A, mode);
	EEPROM.end();
	if (mode < 0 || mode >= MODES_COUNT) // first run
		mode = DEFAULT_MODE_A;
	return mode;
}

Mode loadModeB() {
	Mode mode;
	EEPROM.begin(MEMORY_SIZE);
	EEPROM.get(MEMORY_ADDRESS_B, mode);
	EEPROM.end();
	if (mode < 0 || mode >= MODES_COUNT) // first run
		mode = DEFAULT_MODE_B;
	return mode;
}

void saveModeA(Mode mode) {
	EEPROM.begin(MEMORY_SIZE);
	EEPROM.put(MEMORY_ADDRESS_A, mode);
	EEPROM.commit();
	EEPROM.end();
}

void saveModeB(Mode mode) {
	EEPROM.begin(MEMORY_SIZE);
	EEPROM.put(MEMORY_ADDRESS_B, mode);
	EEPROM.commit();
	EEPROM.end();
}

void formatClock(time_t t, char txt[], bool dots[], bool mode24, bool trailZero) {
	int hours = mode24 ? hour(t) : hourFormat12(t);
	if (hours > 9)
		txt[0] = '0' + (hours / 10);
	else if (trailZero)
		txt[0] = '0';
	txt[1] = '0' + (hours % 10);
	txt[2] = '0' + (minute(t) / 10);
	txt[3] = '0' + (minute(t) % 10);
	dots[3] = (!mode24) && isPM(t);
}

void formatDate(time_t t, char txt[], bool dots[], bool trailZero) {
	if (day(t) > 9)
		txt[0] = '0' + (day(t) / 10);
	else if (trailZero)
		txt[0] = '0';
	txt[1] = '0' + (day(t) % 10);
	txt[2] = '0' + (month(t) / 10);
	txt[3] = '0' + (month(t) % 10);
	dots[1] = true;
}

void formatDateName(time_t t, char txt[], bool dots[], bool trailZero) {
	if (day(t) > 9)
		txt[0] = '0' + (day(t) / 10);
	else if (trailZero)
		txt[0] = '0';
	txt[1] = '0' + (day(t) % 10);
	switch (month(t)) {
		case 1:
			txt[2] = 'J';
			txt[3] = 'n'; break;
		case 2:
			txt[2] = 'F';
			txt[3] = 'v'; break;
		case 3:
			txt[2] = 'M';
			txt[3] = 'r'; break;
		case 4:
			txt[2] = 'A';
			txt[3] = 'b'; break;
		case 5:
			txt[2] = 'M';
			txt[3] = 'i'; break;
		case 6:
			txt[2] = 'J';
			txt[3] = 'n'; break;
		case 7:
			txt[2] = 'J';
			txt[3] = 'l'; break;
		case 8:
			txt[2] = 'A';
			txt[3] = 'g'; break;
		case 9:
			txt[2] = 'S';
			txt[3] = 't'; break;
		case 10:
			txt[2] = 'O';
			txt[3] = 't'; break;
		case 11:
			txt[2] = 'N';
			txt[3] = 'v'; break;
		case 12:
			txt[2] = 'D';
			txt[3] = 'z'; break;
	}
	dots[1] = true;
}

void formatYear(time_t t, char txt[]) {
	txt[0] = '0' + (year(t) / 1000);
	txt[1] = '0' + (year(t) / 100 % 10);
	txt[2] = '0' + (year(t) / 10 % 10);
	txt[3] = '0' + (year(t) % 10);
}

void formatMonth(time_t t, char txt[], bool trailZero) {
	if (month(t) > 9)
		txt[1] = '0' + (month(t) / 10);
	else if (trailZero)
		txt[1] = '0';
	txt[2] = '0' + (month(t) % 10);
}

void formatDay(time_t t, char txt[], bool trailZero) {
	if (day(t) > 9)
		txt[1] = '0' + (day(t) / 10);
	else if (trailZero)
		txt[1] = '0';
	txt[2] = '0' + (day(t) % 10);
}

void formatHour(time_t t, char txt[], bool dots[], bool mode24, bool trailZero) {
	int hours = mode24 ? hour(t) : hourFormat12(t);
	if (hours > 9)
		txt[1] = '0' + (hours / 10);
	else if (trailZero)
		txt[1] = '0';
	txt[2] = '0' + (hours % 10);
	dots[2] = (!mode24) && isPM(t);
}

void formatMinute(time_t t, char txt[], bool trailZero) {
	if (minute(t) > 9)
		txt[1] = '0' + (minute(t) / 10);
	else if (trailZero)
		txt[1] = '0';
	txt[2] = '0' + (minute(t) % 10);
}

void formatSecond(time_t t, char txt[], bool trailZero) {
	if (second(t) > 9)
		txt[1] = '0' + (second(t) / 10);
	else if (trailZero)
		txt[1] = '0';
	txt[2] = '0' + (second(t) % 10);
}

void formatMonthName(time_t t, char txt[]) {
	switch (month(t)) {
		case 1:
			txt[0] = 'J';
			txt[1] = 'a';
			txt[2] = 'n'; break;
		case 2:
			txt[0] = 'F';
			txt[1] = 'e';
			txt[2] = 'v'; break;
		case 3:
			txt[0] = 'M';
			txt[1] = 'a';
			txt[2] = 'r'; break;
		case 4:
			txt[0] = 'A';
			txt[1] = 'b';
			txt[2] = 'r'; break;
		case 5:
			txt[0] = 'M';
			txt[1] = 'a';
			txt[2] = 'i';
			txt[3] = 'o'; break;
		case 6:
			txt[0] = 'J';
			txt[1] = 'u';
			txt[2] = 'n'; break;
		case 7:
			txt[0] = 'J';
			txt[1] = 'u';
			txt[2] = 'l'; break;
		case 8:
			txt[0] = 'A';
			txt[1] = 'g';
			txt[2] = 'o'; break;
		case 9:
			txt[0] = 'S';
			txt[1] = 'e';
			txt[2] = 't'; break;
		case 10:
			txt[0] = 'O';
			txt[1] = 'u';
			txt[2] = 't'; break;
		case 11:
			txt[0] = 'N';
			txt[1] = 'o';
			txt[2] = 'v'; break;
		case 12:
			txt[0] = 'D';
			txt[1] = 'e';
			txt[2] = 'z'; break;
	}
}

void formatWeekday(time_t t, char txt[]) {
	switch (weekday(t)) {
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
			txt[1] = '0' + weekday(t);
			txt[2] = 'F';
			break;
		case 7:
			txt[0] = 'S';
			txt[1] = 'A';
			txt[2] = 'b';
			break;
		case 1:
			txt[0] = 'd';
			txt[1] = 'o';
			txt[2] = 'n';
			break;
	}
}

void formatAmPm(time_t t, char txt[]) {
	txt[1] = isPM(t) ? 'p' : 'a';
	txt[2] = 'm';
}

void formatAmPm(time_t t, char txt[], bool dots[]) {
	formatAmPm(t, txt);
	dots[1] = true;
	dots[2] = true;
}

void format(Mode mode, time_t t, char txt[], bool dots[]) {
	memset(txt, ' ', 4);
	memset(dots, false, 4);

	switch (mode) {
		case DATE_LOOP: // change every 32 seconds
			if (millis() / 32000 % 2)
				formatDate(t, txt, dots, true);
			else
				formatWeekday(t, txt);
			break;
		case CLOCK_24:
			formatClock(t, txt, dots, true, false); break;
		case CLOCK_24_TRAILZERO:
			formatClock(t, txt, dots, true, true); break;
		case CLOCK_12:
			formatClock(t, txt, dots, false, false); break;
		case CLOCK_12_TRAILZERO:
			formatClock(t, txt, dots, false, true); break;
		case DATE:
			formatDate(t, txt, dots, false); break;
		case DATE_TRAILZERO:
			formatDate(t, txt, dots, true); break;
		case DATE_NAME:
			formatDateName(t, txt, dots, false); break;
		case DATE_NAME_TRAILZERO:
			formatDateName(t, txt, dots, true); break;
		case YEAR:
			formatYear(t, txt); break;
		case MONTH:
			formatMonth(t, txt, false); break;
		case MONTH_TRAILZERO:
			formatMonth(t, txt, true); break;
		case DAY:
			formatDay(t, txt, false); break;
		case DAY_TRAILZERO:
			formatDay(t, txt, true); break;
		case HOUR_24:
			formatHour(t, txt, dots, true, false); break;
		case HOUR_24_TRAILZERO:
			formatHour(t, txt, dots, true, true); break;
		case HOUR_12:
			formatHour(t, txt, dots, false, false); break;
		case HOUR_12_TRAILZERO:
			formatHour(t, txt, dots, false, true); break;
		case MINUTE:
			formatMinute(t, txt, false); break;
		case MINUTE_TRAILZERO:
			formatMinute(t, txt, true); break;
		case SECOND:
			formatSecond(t, txt, false); break;
		case SECOND_TRAILZERO:
			formatSecond(t, txt, true); break;
		case MONTH_NAME:
			formatMonthName(t, txt); break;
		case WEEKDAY:
			formatWeekday(t, txt); break;
		case AM_PM:
			formatAmPm(t, txt, dots); break;
		case AM_PM_DOTLESS:
			formatAmPm(t, txt); break;
		case EMPTY:
			break;
	}
}

void setDisplay(time_t t, Mode modeA, Mode modeB, bool synced) { // send to display controller 
	char txtA[4];
	char txtB[4];
	bool dotsA[4];
	bool dotsB[4];

	// format data for current modes and status
	format(modeA, t, txtA, dotsA);
	format(modeB, t, txtB, dotsB);
	if (!synced)
		dotsA[0] = true; // not synced signal
	if (modeB == CLOCK_24 || modeB == CLOCK_12 || modeB == CLOCK_24_TRAILZERO || modeB == CLOCK_12_TRAILZERO)
		dotsB[1] = true; // bottom display doesnt have colon, turn on middle dot led if clocks
 
	// send data
	Wire.beginTransmission(DISPLAY_CTL_ADDRESS);
	Wire.write(txtA, 4);
	Wire.write(txtB, 4);
	for (int i = 0; i < 4; i++)
		Wire.write(dotsA[i]);
	for (int i = 0; i < 4; i++)
		Wire.write(dotsB[i]);
	Wire.endTransmission();
}

void setColon(Mode mode) {
	bool on = mode == CLOCK_24 || mode == CLOCK_12 || mode == CLOCK_24_TRAILZERO || mode == CLOCK_12_TRAILZERO;
	Wire.beginTransmission(DISPLAY_CTL_ADDRESS);
	Wire.write(on);
	Wire.endTransmission();
}

void setLed(bool on) {
	Wire.beginTransmission(DISPLAY_CTL_ADDRESS);
	Wire.write(on);
	Wire.write(0); // just to have different size from colon msg
	Wire.endTransmission();
}

time_t getServerTime() { // return unix time from ntp or 0 if error
	WiFiUDP udp;
	udp.begin(LOCAL_PORT);

	// form NTP request
	byte packet[NTP_PACKET_SIZE];
	memset(packet, 0, NTP_PACKET_SIZE);
	packet[0] = 0b11100011; // li, vn, mode
	packet[1] = 0; // strat
	packet[2] = 6; // poll
	packet[3] = 0xEC; // prec
	packet[12] = 49;
	packet[13] = 0x4E;
	packet[14] = 49;
	packet[15] = 52;

	// send request to server
	IPAddress address;
	WiFi.hostByName(NTP_SERVER, address);
	udp.beginPacket(address, NTP_PORT);
	udp.write(packet, NTP_PACKET_SIZE);
	udp.endPacket();

	// wait response from server
	unsigned long start = millis();
	while (udp.parsePacket() < NTP_PACKET_SIZE && millis() - start < NTP_TIMEOUT)
		yield();

	// read response from server
	unsigned long unixTime = 0;
	if (udp.available() == NTP_PACKET_SIZE) {
		udp.read(packet, NTP_PACKET_SIZE);
		unsigned long ntpTime;
		ntpTime = (unsigned long)packet[40] << 24;
		ntpTime |= (unsigned long)packet[41] << 16;
		ntpTime |= (unsigned long)packet[42] << 8;
		ntpTime |= (unsigned long)packet[43];
		unixTime = ntpTime - 2208988800UL;
	}

	udp.stop();
	return unixTime;
}

bool syncLocalTime() { // syncronize local time with server
	syncing = true;

	// connect
	WiFi.begin(WIFI_SSID, WIFI_KEY);
	int tries = 0;
	while (WiFi.status() != WL_CONNECTED && tries++ < WIFI_TIMEOUT*2) {
		setLed(tries % 2); // led blinking while connecting
		delay (500);
	}
	
	// check error
	if (WiFi.status() != WL_CONNECTED) {
		WiFi.disconnect();
		setLed(false);
		return false;
	}

	setLed(true); // led on while connected

	// retrieve time
	time_t t = 0;
	tries = 0;
	while (t == 0 && tries++ < SYNC_TIMEOUT*2) {
		t = getServerTime();
	}

	// check error
	if (t == 0) {
		WiFi.disconnect();
		setLed(false);
		return false;
	}

	setTime(t);

	// disconnect
	WiFi.disconnect();
	setLed(false);

	syncing = false;
	return true;
}

void buttonAReleasedInt() {
	buttonATime = millis();
	attachInterrupt(digitalPinToInterrupt(BUTTON_A_PIN), buttonAPressedInt, FALLING);
}

void buttonBReleasedInt() {
	buttonBTime = millis();
	attachInterrupt(digitalPinToInterrupt(BUTTON_B_PIN), buttonBPressedInt, FALLING);
}

void buttonAPressedInt() {
	if (!syncing) // ignore if syncing
		if (millis() - buttonATime > DEBOUNCE_TIME) { // debounce
			buttonAPressed = true;
			attachInterrupt(digitalPinToInterrupt(BUTTON_A_PIN), buttonAReleasedInt, RISING);
		}
}

void buttonBPressedInt() {
	if (!syncing) // ignore if syncing
		if (millis() - buttonBTime > DEBOUNCE_TIME) { // debounce
			buttonBPressed = true;
			attachInterrupt(digitalPinToInterrupt(BUTTON_B_PIN), buttonBReleasedInt, RISING);
		}
}

void setup(){
	Wire.begin(SDA_PIN, SCL_PIN);
	WiFi.mode(WIFI_STA);
	WiFi.persistent(false);
	WiFi.hostname(HOSTNAME);
	bool s = false;
	while (!s)
		s = syncLocalTime();
	buttonAPressed = buttonBPressed = false;
	buttonATime = buttonBTime = 0;
	pinMode(BUTTON_A_PIN, INPUT);
	pinMode(BUTTON_B_PIN, INPUT);
	attachInterrupt(digitalPinToInterrupt(BUTTON_A_PIN), buttonAPressedInt, FALLING);
	attachInterrupt(digitalPinToInterrupt(BUTTON_B_PIN), buttonBPressedInt, FALLING);
}

void loop() {
	static bool synced = true;
	static time_t prevTime = 0;
	static Mode modeA = loadModeA();
	static Mode modeB = loadModeB();

	time_t t = now();

	if (t != prevTime) { // time changed
		prevTime = t;
		setDisplay(pt.toLocal(t), modeA, modeB, synced);
		setColon(modeA);
	}

	if (buttonAPressed) { // next mode requested on display A
		modeA = Mode((modeA + 1) % MODES_COUNT); // iterate mode
		setDisplay(pt.toLocal(t), modeA, modeB, synced);
		saveModeA(modeA);
		buttonAPressed = false;
	}

	if (buttonBPressed) { // next mode requested on display B
		modeB = Mode((modeB + 1) % MODES_COUNT); // iterate mode
		setDisplay(pt.toLocal(t), modeA, modeB, synced);
		saveModeB(modeB);
		buttonBPressed = false;
	}

	if (hour(t) == SYNC_HOUR && minute(t) == 0 && second(t) == 0) { // auto sync
		synced = syncLocalTime();
	}
}
