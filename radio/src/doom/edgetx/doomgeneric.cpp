#include "doomgeneric.h"

#include "display.h"
#include "model_init.h"
#include "opentx.h"
#include "stdio.h"

#include "board.h"
#include "doomkeys.h"
#include "doomtype.h"

#ifdef SIMU
#include <sys/time.h>
#include <unistd.h>
#endif

// The radio's keys, in the order readKeys() reports them. EdgeTX names them
// too, but its KEY_ENTER and Doom's are different numbers, so the enum here
// avoids having to mention them.
enum RadioKey {
  RADIO_PAGE_PREV,
  RADIO_PAGE_NEXT,
  RADIO_WHEEL,  // the scroll wheel's push
  RADIO_MDL,
  RADIO_RTN,
  RADIO_TELE,
  RADIO_SYS,
};

// What the keys do with a menu open: between them they can work any of it.
#ifdef SIMU
const uint8_t keyboardMap[] = {KEY_ESCAPE,   KEY_USE,       KEY_ENTER,
                               KEY_UPARROW,  KEY_DOWNARROW, KEY_RIGHTARROW,
                               KEY_LEFTARROW};
#else
const uint8_t keyboardMap[] = {KEY_DOWNARROW,   // PAGE<
                               KEY_UPARROW,     // PAGE>
                               KEY_ENTER,       // wheel push
                               KEY_RIGHTARROW,  // MDL
                               KEY_ESCAPE,      // RTN
                               KEY_ESCAPE,      // TELE
                               KEY_LEFTARROW};  // SYS
#endif

rotenc_t oldRotencValue;

extern boolean menuactive;
extern int key_prevweapon;
extern int key_nextweapon;

// The scroll wheel steps the player sideways. A click becomes a short burst of
// strafing rather than a single tic of it, which would barely show; spinning
// the wheel faster stacks the bursts up into a continuous sidestep.
//
// Every count the encoder reports is a step. Counts are not rounded up into
// whole detents first: EdgeTX's own navigation does that (dividing by
// ROTARY_ENCODER_GRANULARITY), but a click of this wheel is a single count, so
// rounding would swallow it and stepping would take two clicks.
#define DOOM_STRAFE_TICS_PER_STEP 3

// Two thirds of a second of strafing still owed. Keeps a fast spin from
// running on long after the wheel has stopped.
#define DOOM_MAX_STRAFE_TICS 24

static int32_t rotencSteps;  // counts read but not acted on yet
static int32_t strafeTics;   // tics of strafing still to send, + is right

static void pollRotaryEncoder() {
  rotenc_t value = rotencValue;
  rotencSteps += (int32_t)(value - oldRotencValue);
  oldRotencValue = value;

  const int32_t maxSteps = DOOM_MAX_STRAFE_TICS / DOOM_STRAFE_TICS_PER_STEP;

  if (rotencSteps > maxSteps) {
    rotencSteps = maxSteps;
  } else if (rotencSteps < -maxSteps) {
    rotencSteps = -maxSteps;
  }
}

// There are only seven keys, so each one does something else once the menu is
// closed and it is no longer needed to work it.
static unsigned char inGameKey(int radioKey) {
#ifdef SIMU
  // The simulator has a whole keyboard, so only the menu key doubles up.
  return keyboardMap[radioKey] == KEY_ENTER ? KEY_FIRE : keyboardMap[radioKey];
#else
  switch (radioKey) {
    case RADIO_PAGE_PREV: return (unsigned char)key_prevweapon;
    case RADIO_PAGE_NEXT: return (unsigned char)key_nextweapon;
    case RADIO_WHEEL:     return KEY_FIRE;
    case RADIO_MDL:       return (unsigned char)key_nextweapon;
    case RADIO_RTN:       return KEY_USE;
    case RADIO_TELE:      return KEY_ESCAPE;  // opens the menu
    case RADIO_SYS:       return KEY_FIRE;
    default:              return keyboardMap[radioKey];
  }
#endif
}

// generalDefault() clears the stick calibration, and an axis with no
// calibration reads full scale in one direction, which would walk the player
// into the nearest wall for ever. Calibrate here instead of reading the
// radio's own settings: the gimbal is spring centred and nobody can be holding
// it this early, so whatever it reads now is the centre.
//
// The travel is an estimate. Being short only means the stick reaches full
// speed before the end of its throw, which costs nothing -- Doom clamps
// movement anyway. A stick that does not centre (the throttle, unused here)
// just ends up centred wherever it was left.
#define STICK_TRAVEL 600  // in anaIn() counts, which run 0..2047

static void calibrateSticks() {
  getADC();  // the mixer task is still paused, so nothing else is sampling

  for (int i = 0; i < NUM_STICKS; i++) {
    g_eeGeneral.calib[i].mid = anaIn(i);
    g_eeGeneral.calib[i].spanPos = STICK_TRAVEL;
    g_eeGeneral.calib[i].spanNeg = STICK_TRAVEL;
  }
}

void dg_Create() {
  pwrOn();
#if defined(SPORT_UPDATE_PWR_GPIO)
  SPORT_UPDATE_POWER_INIT();
#endif
  DG_Init();
  if (!sdMounted())
    sdInit();
  generalDefault();
  g_eeGeneral.inactivityTimer = 0;
  setModelDefaults();
  logsInit();
#if defined(DEBUG) && defined(STM32) && !defined(SIMU)
  initSerialPorts();
  if (usbPlugged()) {
    setSelectedUsbMode(USB_SERIAL_MODE);
    serialInit(SP_VCP, serialGetMode(SP_VCP));
    usbStart();
  }
#endif
  // loadRadioSettings();
  calibrateSticks();
  resetBacklightTimeout();
  WDG_ENABLE(WDG_DURATION);
  startPulses();
  oldRotencValue = rotencValue;
}

int AD_RV = 0;  // right gimbal, vertical
int AD_RH = 0;  // right gimbal, horizontal

// Ignore this much slack around the centre, so a gimbal resting off centre
// does not walk the player into a wall while nobody is touching it.
#define STICK_DEADZONE (JOYAXIS_MAX / 12)

int map_values(int x, int in_min, int in_max, int out_min, int out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// CONVERT_MODE(2) and CONVERT_MODE(3) are the right gimbal's vertical and
// horizontal axes whichever stick mode the radio is set to -- the same way the
// main view picks out the right stick to draw. Both read positive when the
// stick is pushed up or to the right.
static int readStick(uint8_t axis) {
  int value = map_values(calibratedAnalogs[CONVERT_MODE(axis)], -RESX, RESX,
                         -JOYAXIS_MAX, JOYAXIS_MAX);

  // Pick up from a standstill at the edge of the deadzone rather than jumping
  // straight to a twelfth of full speed.
  if (value > STICK_DEADZONE) {
    return (value - STICK_DEADZONE) * JOYAXIS_MAX /
           (JOYAXIS_MAX - STICK_DEADZONE);
  }
  if (value < -STICK_DEADZONE) {
    return (value + STICK_DEADZONE) * JOYAXIS_MAX /
           (JOYAXIS_MAX - STICK_DEADZONE);
  }
  return 0;
}

void button_update_loop() {
  // The mixer task keeps calibratedAnalogs up to date, so there is nothing to
  // sample here.
  AD_RV = readStick(2);
  AD_RH = readStick(3);

  if (pwrPressed()) {
    boardOff();
  }
}

void DG_SleepMs(uint32_t ms) {
#ifndef SIMU
  delay_ms(ms);
#else
  usleep(ms * 1000);
#endif
}

uint32_t DG_GetTicksMs() {
  return RTOS_GET_MS();
}

int DG_GetKey(int* pressed, unsigned char* key) {
  static uint32_t oldKeys = 0;
  static unsigned char sentKeys[sizeof(keyboardMap)] = {0};
  static unsigned char scrollKey = 0;

  pollRotaryEncoder();

  // Release of the key synthesized for the previous menu scroll step.
  if (scrollKey) {
    *key = scrollKey;
    *pressed = 0;
    scrollKey = 0;
    return 1;
  }

  // In the menus the wheel moves the cursor instead of stepping.
  if (menuactive && rotencSteps != 0) {
    if (rotencSteps > 0) {
      *key = KEY_DOWNARROW;
      rotencSteps--;
    } else {
      *key = KEY_UPARROW;
      rotencSteps++;
    }
    scrollKey = *key;
    *pressed = 1;
    return 1;
  }

  auto keys = readKeys();

  for (auto i = 0; i < sizeof(keyboardMap); i++) {
    uint32_t k = 1 << i;
    if ((keys & k) && !(oldKeys & k)) {
      // Remember what was sent: the menu may open or close while the key is
      // held, and releasing under the other mapping would leave it stuck down.
      sentKeys[i] = menuactive ? keyboardMap[i] : inGameKey(i);
      *key = sentKeys[i];
      *pressed = 1;
      oldKeys |= k;
      return 1;
    }
    if (!(keys & k) && (oldKeys & k)) {
      *key = sentKeys[i];
      *pressed = 0;
      oldKeys = oldKeys & (~k);

      return 1;
    }
  }

  return 0;
}

// Called once per tic, so the burst a click owes is paid out one tic at a
// time.
int DG_GetStrafe() {
  if (menuactive) {
    strafeTics = 0;  // the wheel is driving the menu cursor
    return 0;
  }

  if (rotencSteps != 0) {
    int32_t tics = rotencSteps * DOOM_STRAFE_TICS_PER_STEP;
    rotencSteps = 0;

    // Turning the wheel back the other way cancels what is left of the step
    // it interrupted rather than being queued up behind it.
    strafeTics = ((tics > 0) == (strafeTics > 0)) ? strafeTics + tics : tics;

    if (strafeTics > DOOM_MAX_STRAFE_TICS) {
      strafeTics = DOOM_MAX_STRAFE_TICS;
    } else if (strafeTics < -DOOM_MAX_STRAFE_TICS) {
      strafeTics = -DOOM_MAX_STRAFE_TICS;
    }
  }

  if (strafeTics > 0) {
    strafeTics--;
    return JOYAXIS_MAX;
  }
  if (strafeTics < 0) {
    strafeTics++;
    return -JOYAXIS_MAX;
  }
  return 0;
}
