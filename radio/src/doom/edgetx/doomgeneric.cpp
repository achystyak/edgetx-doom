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

#ifdef SIMU
const uint8_t keyboardMap[] = {KEY_ESCAPE,   KEY_USE,       KEY_ENTER,
                               KEY_UPARROW,  KEY_DOWNARROW, KEY_RIGHTARROW,
                               KEY_LEFTARROW};
#else
const uint8_t keyboardMap[] = {KEY_DOWNARROW,  KEY_UPARROW, KEY_ENTER,
                               KEY_RIGHTARROW, KEY_ESCAPE,  KEY_USE,
                               KEY_LEFTARROW};
#endif

rotenc_t oldRotencValue;

extern boolean menuactive;
extern int key_nextweapon;

// The scroll wheel turns the player. Its steps are injected as mouse movement
// instead of left/right key presses: a key has to stay down for a whole tic to
// turn at all, while mouse movement is summed into the next ticcmd, so turning
// follows how far the wheel was spun rather than the frame rate.
//
// Every count the encoder reports turns, so that a single click of the wheel
// moves the view the way tapping a turn key would. Counts are not rounded up
// into whole detents first: EdgeTX's own navigation does that (dividing by
// ROTARY_ENCODER_GRANULARITY), but a click of this wheel is a single count, so
// rounding would swallow it and turning would take two clicks.
//
// G_BuildTiccmd makes 8 units of angleturn out of one mouse unit and a full
// circle is 65536 of angleturn, so this is 160 * 8 / 65536 * 360 = ~7 degrees
// per click -- the same as one tic of Doom's own fast keyboard turn -- scaled
// by the mouse sensitivity setting ((sens + 5) / 10).
#define DOOM_TURN_PER_STEP 160

// Roughly half a circle in one frame. Keeps a fast spin from overflowing the
// signed short angleturn field.
#define DOOM_MAX_TURN_STEPS 24

static int32_t rotencSteps;  // counts read but not handed to Doom yet

static void pollRotaryEncoder() {
  rotenc_t value = rotencValue;
  rotencSteps += (int32_t)(value - oldRotencValue);
  oldRotencValue = value;

  if (rotencSteps > DOOM_MAX_TURN_STEPS) {
    rotencSteps = DOOM_MAX_TURN_STEPS;
  } else if (rotencSteps < -DOOM_MAX_TURN_STEPS) {
    rotencSteps = -DOOM_MAX_TURN_STEPS;
  }
}

// There are only seven keys, so the menu keys double as controls once the menu
// is closed. Left and right keep their menu meaning so the option sliders stay
// usable.
static unsigned char inGameKey(unsigned char key) {
  switch (key) {
    case KEY_ENTER:      return KEY_FIRE;  // scroll wheel push
    case KEY_LEFTARROW:  return KEY_FIRE;  // SYS
    case KEY_RIGHTARROW: return (unsigned char)key_nextweapon;  // MDL
    default:             return key;
  }
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

  // In the menus the wheel moves the cursor instead of turning.
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
      sentKeys[i] = menuactive ? keyboardMap[i] : inGameKey(keyboardMap[i]);
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

int DG_GetTurn() {
  if (menuactive) {
    return 0;  // the wheel is driving the menu cursor
  }

  int32_t steps = rotencSteps;
  rotencSteps = 0;
  return steps * DOOM_TURN_PER_STEP;
}
