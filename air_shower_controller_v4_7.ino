/*
  Intelligent Air Shower Controller
  Arduino Mega 2560
  Software: V4.7 FINAL FIXED

  INPUTS
    D30 S1          HIGH = person detected
    D31 S2          HIGH = person detected
    D32 Outer limit HIGH = door closed, LOW = door open
    D33 Inner limit HIGH = door closed, LOW = door open
    D34 E-STOP      HIGH = active
    D35 RESET       HIGH = pressed

  OUTPUTS
    D22 Outer lock  HIGH = LOCK, LOW = UNLOCK
    D23 Inner lock  HIGH = LOCK, LOW = UNLOCK
    D24 Blower      HIGH = ON, LOW = OFF
    D25 Lighting    LOW = ON, HIGH = OFF
    D26 Buzzer      LOW = ON, HIGH = OFF

  IMPORTANT:
    Arduino pins are 5 V logic only.
    12 V sensors/relay inputs must be isolated/interface-converted.
    Do not connect 12/24 V directly to Arduino I/O.
*/

const byte RELAY_LOCK_OUTER = 22;
const byte RELAY_LOCK_INNER = 23;
const byte RELAY_BLOWER     = 24;
const byte RELAY_LIGHT      = 25;
const byte BUZZER           = 26;

const byte SENSOR_1     = 30;
const byte SENSOR_2     = 31;
const byte LIMIT_OUTER  = 32;
const byte LIMIT_INNER  = 33;
const byte ESTOP        = 34;
const byte RESET_BTN    = 35;

const byte PERSON_DETECTED = HIGH;
const byte DOOR_CLOSED     = HIGH;
const byte DOOR_OPEN       = LOW;
const byte ESTOP_ACTIVE    = HIGH;

const unsigned long SHOWER_TIME         = 20000UL;
const unsigned long DOOR_OPEN_TIMEOUT   = 30000UL;
const unsigned long ENTRY_TIMEOUT       = 15000UL;
const unsigned long AFTER_SHOWER_TIMEOUT= 30000UL;
const unsigned long DEBOUNCE_DELAY      = 50UL;
const unsigned long RESET_HOLD_TIME     = 1000UL;

enum State {
  IDLE,
  OUTER_DOOR_ACTIVE,
  INNER_DOOR_ACTIVE,
  ENTRY_WAIT_PERSON,
  SHOWER,
  AFTER_SHOWER,
  EXIT_SHOWER,
  ALARM
};

State state = IDLE;

bool s1 = false;
bool s2 = false;
bool outerClosed = false;
bool innerClosed = false;
bool eStop = false;
bool resetPressed = false;

bool entryDetected = false;
bool personDetected = false;

unsigned long stateStart = 0;
unsigned long resetStart = 0;
unsigned long lastDebug = 0;

const char* stateName(State s)
{
  switch (s) {
    case IDLE:               return "IDLE";
    case OUTER_DOOR_ACTIVE:  return "OUTER_DOOR_ACTIVE";
    case INNER_DOOR_ACTIVE:  return "INNER_DOOR_ACTIVE";
    case ENTRY_WAIT_PERSON:  return "ENTRY_WAIT_PERSON";
    case SHOWER:             return "SHOWER";
    case AFTER_SHOWER:       return "AFTER_SHOWER";
    case EXIT_SHOWER:        return "EXIT_SHOWER";
    case ALARM:              return "ALARM";
    default:                 return "UNKNOWN";
  }
}

void lockOuter()   { digitalWrite(RELAY_LOCK_OUTER, HIGH); }
void unlockOuter() { digitalWrite(RELAY_LOCK_OUTER, LOW); }

void lockInner()   { digitalWrite(RELAY_LOCK_INNER, HIGH); }
void unlockInner() { digitalWrite(RELAY_LOCK_INNER, LOW); }

void blowerON()    { digitalWrite(RELAY_BLOWER, HIGH); }
void blowerOFF()   { digitalWrite(RELAY_BLOWER, LOW); }

void lightON()     { digitalWrite(RELAY_LIGHT, LOW); }
void lightOFF()    { digitalWrite(RELAY_LIGHT, HIGH); }

void buzzerON()    { digitalWrite(BUZZER, LOW); }
void buzzerOFF()   { digitalWrite(BUZZER, HIGH); }

void readInputs()
{
  s1 = (digitalRead(SENSOR_1) == PERSON_DETECTED);
  s2 = (digitalRead(SENSOR_2) == PERSON_DETECTED);

  outerClosed = (digitalRead(LIMIT_OUTER) == DOOR_CLOSED);
  innerClosed = (digitalRead(LIMIT_INNER) == DOOR_CLOSED);

  eStop = (digitalRead(ESTOP) == ESTOP_ACTIVE);
  resetPressed = (digitalRead(RESET_BTN) == HIGH);
}

void enterState(State newState)
{
  state = newState;
  stateStart = millis();
}

void enterAlarm()
{
  state = ALARM;
  stateStart = millis();

  blowerOFF();
  lockOuter();
  lockInner();
  buzzerON();
  lightOFF();
}

bool resetCycle()
{
  // Never reset/unlock while the person latch is still active.
  if (personDetected) {
    enterAlarm();
    return false;
  }

  entryDetected = false;
  personDetected = false;

  blowerOFF();
  buzzerOFF();
  unlockOuter();
  unlockInner();
  lightON();

  enterState(IDLE);
  return true;
}

void processS2Latch()
{
  // S2 is latched only after S1 has confirmed entry and the
  // state machine is waiting for the person to reach the shower.
  if (state == ENTRY_WAIT_PERSON && s2) {
    personDetected = true;
    enterState(SHOWER);
    blowerON();
  }
}

void stateIdle()
{
  blowerOFF();
  buzzerOFF();
  lightON();

  if (!outerClosed) {
    // Outer door opened first: immediately lock inner door.
    lockInner();
    enterState(OUTER_DOOR_ACTIVE);
    return;
  }

  if (!innerClosed) {
    // Inner door opened first: direct exit.
    lockOuter();
    enterState(INNER_DOOR_ACTIVE);
    return;
  }

  unlockOuter();
  unlockInner();

  if (s1) {
    entryDetected = true;
    enterState(OUTER_DOOR_ACTIVE);
    lockInner();
  }
}

void stateOuterDoor()
{
  lightON();
  buzzerOFF();

  // Entry path: S1 has confirmed a person.
  if (entryDetected) {
    lockInner();

    if (outerClosed) {
      lockOuter();
      entryDetected = true;
      enterState(ENTRY_WAIT_PERSON);
      return;
    }

    if (millis() - stateStart >= DOOR_OPEN_TIMEOUT) {
      enterAlarm();
      return;
    }

    return;
  }

  // No entry latch: this was an outer-door activity.
  lockInner();

  if (outerClosed) {
    unlockOuter();
    unlockInner();
    enterState(IDLE);
    return;
  }

  if (millis() - stateStart >= DOOR_OPEN_TIMEOUT) {
    enterAlarm();
  }
}

void stateInnerDoor()
{
  lightON();
  blowerOFF();
  lockOuter();

  if (innerClosed) {
    unlockOuter();
    enterState(IDLE);
    return;
  }

  if (millis() - stateStart >= DOOR_OPEN_TIMEOUT) {
    enterAlarm();
  }
}

void stateEntryWait()
{
  lightON();
  blowerOFF();
  lockOuter();
  lockInner();

  // S2 is handled by processS2Latch().
  if (millis() - stateStart >= ENTRY_TIMEOUT) {
    enterAlarm();
  }
}

void stateShower()
{
  lightON();
  lockOuter();
  lockInner();
  blowerON();

  if (millis() - stateStart >= SHOWER_TIME) {
    blowerOFF();
    enterState(AFTER_SHOWER);
    return;
  }
}

void stateAfterShower()
{
  lightON();
  blowerOFF();
  lockOuter();
  unlockInner();

  // Person must open inner door after shower.
  if (!innerClosed) {
    enterState(EXIT_SHOWER);
    return;
  }

  if (millis() - stateStart >= AFTER_SHOWER_TIMEOUT) {
    enterAlarm();
  }
}

void stateExitShower()
{
  lightON();
  blowerOFF();
  lockOuter();
  unlockInner();

  // Person is considered out only after inner door closes.
  if (innerClosed) {
    personDetected = false;
    entryDetected = false;

    unlockOuter();
    unlockInner();
    enterState(IDLE);
    return;
  }

  if (millis() - stateStart >= DOOR_OPEN_TIMEOUT) {
    enterAlarm();
  }
}

void stateAlarm()
{
  blowerOFF();
  lockOuter();
  lockInner();
  buzzerON();
  lightOFF();

  // Reset conditions:
  // 1. E-stop normal
  // 2. Both doors closed
  // 3. S2 not detecting
  // 4. RESET held for 1 second
  if (!eStop && outerClosed && innerClosed && !s2) {
    if (resetPressed) {
      if (resetStart == 0) {
        resetStart = millis();
      }

      if (millis() - resetStart >= RESET_HOLD_TIME) {
        resetStart = 0;
        personDetected = false;
        entryDetected = false;
        blowerOFF();
        buzzerOFF();
        unlockOuter();
        unlockInner();
        lightON();
        enterState(IDLE);
      }
    } else {
      resetStart = 0;
    }
  } else {
    resetStart = 0;
  }
}

void processState()
{
  switch (state) {
    case IDLE:
      stateIdle();
      break;

    case OUTER_DOOR_ACTIVE:
      stateOuterDoor();
      break;

    case INNER_DOOR_ACTIVE:
      stateInnerDoor();
      break;

    case ENTRY_WAIT_PERSON:
      stateEntryWait();
      break;

    case SHOWER:
      stateShower();
      break;

    case AFTER_SHOWER:
      stateAfterShower();
      break;

    case EXIT_SHOWER:
      stateExitShower();
      break;

    case ALARM:
      stateAlarm();
      break;
  }
}

void printDebug()
{
  if (millis() - lastDebug < 500) return;
  lastDebug = millis();

  Serial.print(F("STATE="));
  Serial.print(stateName(state));

  Serial.print(F(" S1="));
  Serial.print(s1);

  Serial.print(F(" S2="));
  Serial.print(s2);

  Serial.print(F(" OUTER_CLOSED="));
  Serial.print(outerClosed);

  Serial.print(F(" INNER_CLOSED="));
  Serial.print(innerClosed);

  Serial.print(F(" ESTOP="));
  Serial.print(eStop);

  Serial.print(F(" RESET="));
  Serial.print(resetPressed);

  Serial.print(F(" ENTRY_LATCH="));
  Serial.print(entryDetected);

  Serial.print(F(" PERSON_LATCH="));
  Serial.println(personDetected);
}

void setup()
{
  pinMode(RELAY_LOCK_OUTER, OUTPUT);
  pinMode(RELAY_LOCK_INNER, OUTPUT);
  pinMode(RELAY_BLOWER, OUTPUT);
  pinMode(RELAY_LIGHT, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(SENSOR_1, INPUT);
  pinMode(SENSOR_2, INPUT);
  pinMode(LIMIT_OUTER, INPUT);
  pinMode(LIMIT_INNER, INPUT);
  pinMode(ESTOP, INPUT);
  pinMode(RESET_BTN, INPUT);

  // Safe initial output condition.
  lockOuter();
  lockInner();
  blowerOFF();
  lightOFF();
  buzzerOFF();

  Serial.begin(9600);
  delay(100);

  readInputs();

  if (eStop) {
    enterAlarm();
  } else {
    unlockOuter();
    unlockInner();
    lightON();
    enterState(IDLE);
  }

  Serial.println(F("Intelligent Air Shower Controller V4.7"));
  Serial.println(F("FINAL FIXED - Arduino Mega 2560"));
}

void loop()
{
  readInputs();

  if (eStop && state != ALARM) {
    enterAlarm();
  }

  processS2Latch();
  processState();
  printDebug();
}
