/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2009 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_syswm.h"
#include "SDL_config.h"
#include "SDL.h"
#include "../../events/SDL_sysevents.h"
#include "../../events/SDL_events_c.h"
#include "SDL_keysym.h"

#include "SDL_playbookvideo.h"
#include "SDL_playbookvideo_c.h"
#include "SDL_playbookevents_c.h"

#include <bps/bps.h>
#include <bps/screen.h>
#include <bps/event.h>
#include <bps/orientation.h>
#include <bps/navigator.h>
#include "touchcontroloverlay.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static int bShowConfigWindow;
static int rc;
#define SCREEN_API(x, y) rc = x; \
    if (rc) fprintf(stderr, "\n%s in %s: %d", y, __FUNCTION__, errno)

#define SDLK_PB_TILDE 160 // Conflicts with SDLK_WORLD_0.
static SDL_keysym Playbook_Keycodes[256];
static SDLKey *Playbook_specialsyms;

extern int  SDL_JoystickInit(void);

//#define TOUCHPAD_SIMULATE 1 // Still experimental
#ifdef TOUCHPAD_SIMULATE
struct TouchState {
	int oldPos[2];
	int pending[2];
	int mask;
	int moveId;
	int leftDown;
	int leftId;
	int rightDown;
	int rightId;
};

static struct TouchState state = { {0, 0}, {0, 0}, 0, -1, 0, -1, 0, -1};
#endif
struct TouchEvent {
	int pending;
	int touching;
	int pos[2];
};
static struct TouchEvent moveEvent;

#if 0
// Structure representing a game controller.
typedef struct GameController_t {
    // Static device info.
    screen_device_t handle;
    int type;
    int analogCount;
    int buttonCount;
    char id[64];
    char vendor[64];
    char product[64];

    // Current state.
    int buttons;
    int analog0[3];
    int analog1[3];

    // Text to display to the user about this controller.
    char deviceString[256];
    char buttonsString[128];
    char analog0String[128];
    char analog1String[128];

    int  dpadPressState[4];
    int  buttonPressState[24];

    bool bReMap;

    SDL_Joystick *joypad;
} GameController_t;

// Controller information.
#define MAX_CONTROLLERS  2
static GameController_t gGameController[MAX_CONTROLLERS];

#define GAME_DPAD_MASK      \
	(SCREEN_DPAD_UP_GAME_BUTTON   | \
	 SCREEN_DPAD_DOWN_GAME_BUTTON | \
	 SCREEN_DPAD_LEFT_GAME_BUTTON | \
	 SCREEN_DPAD_RIGHT_GAME_BUTTON)

#define GAME_BUTTON_MASK    \
	(SCREEN_A_GAME_BUTTON | \
	 SCREEN_B_GAME_BUTTON | \
	 SCREEN_C_GAME_BUTTON | \
	 SCREEN_X_GAME_BUTTON | \
	 SCREEN_Y_GAME_BUTTON | \
	 SCREEN_Z_GAME_BUTTON | \
	 SCREEN_L1_GAME_BUTTON| \
	 SCREEN_L2_GAME_BUTTON| \
	 SCREEN_L3_GAME_BUTTON| \
	 SCREEN_R1_GAME_BUTTON| \
	 SCREEN_R2_GAME_BUTTON| \
	 SCREEN_R3_GAME_BUTTON  )

#define GAME_MENU_MASK      \
	(SCREEN_MENU1_GAME_BUTTON | \
	 SCREEN_MENU2_GAME_BUTTON | \
	 SCREEN_MENU3_GAME_BUTTON | \
	 SCREEN_MENU4_GAME_BUTTON   )

static void loadController(GameController_t* controller)
{
    // Query libscreen for information about this device.
    SCREEN_API(screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_TYPE, &controller->type), "SCREEN_PROPERTY_TYPE");
    SCREEN_API(screen_get_device_property_cv(controller->handle, SCREEN_PROPERTY_ID_STRING, sizeof(controller->id), controller->id), "SCREEN_PROPERTY_ID_STRING");
    SCREEN_API(screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_BUTTON_COUNT, &controller->buttonCount), "SCREEN_PROPERTY_BUTTON_COUNT");

    // Check for the existence of analog sticks.
    if (!screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_ANALOG0, controller->analog0)) {
    	++controller->analogCount;
    }

    if (!screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_ANALOG1, controller->analog0)) {
    	++controller->analogCount;
    }

    if (controller->type == SCREEN_EVENT_GAMEPAD) {
        sprintf(controller->deviceString, "Gamepad device ID: %s", controller->id);
        SLOG("[Gamepad Detected]: %s\n", controller->deviceString);

        if (0 == strcmp("0-0000-0000-1.27", controller->id))
        {
        	controller->bReMap = true;
        	SLOG("Incompatable gamepad detected, Gamepad remapped is used!!");
        }
    } else {
        sprintf(controller->deviceString, "Joystick device: %s", controller->id);
        SLOG("[Joystick Detected]: %s\n", controller->deviceString);
    }

    controller->joypad = SDL_JoystickOpen(0);
}

static void discoverControllers(screen_context_t screenCtx)
{
    // Get an array of all available devices.
    int deviceCount;
    int i;
    int controllerIndex = 0;

    SCREEN_API(screen_get_context_property_iv(screenCtx, SCREEN_PROPERTY_DEVICE_COUNT, &deviceCount), "SCREEN_PROPERTY_DEVICE_COUNT");
    screen_device_t* devices = (screen_device_t*)calloc(deviceCount, sizeof(screen_device_t));
    SCREEN_API(screen_get_context_property_pv(screenCtx, SCREEN_PROPERTY_DEVICES, (void**)devices), "SCREEN_PROPERTY_DEVICES");

    // Scan the list for gamepad and joystick devices.
    for (i = 0; i < deviceCount; i++) {
        int type;
        SCREEN_API(screen_get_device_property_iv(devices[i], SCREEN_PROPERTY_TYPE, &type), "SCREEN_PROPERTY_TYPE");

        if (!rc && (type == SCREEN_EVENT_GAMEPAD || type == SCREEN_EVENT_JOYSTICK)) {
            // Assign this device to control Player 1 or Player 2.
        	GameController_t* controller = &gGameController[controllerIndex];
        	memset(controller, 0, sizeof(GameController_t));
            controller->handle = devices[i];
            loadController(controller);

            // We'll just use the first compatible devices we find.
            controllerIndex++;
            if (controllerIndex == MAX_CONTROLLERS) {
                break;
            }
        }
    }

    free(devices);
}

//
// TODO: This is currently hard coded to KEY entry. Should send to SDL joystick event to handle
//
static void handleGameControllerEvent(screen_event_t event)
{
    screen_device_t   device;
    GameController_t* controller = NULL;
	SDL_keysym keysym;
	int        scancodes[4] = {72, 75, 77, 80}; // From DosBox, keyboard.cpp
	int        symcodes[4]  = {SDLK_w, SDLK_s, SDLK_d, SDLK_a};
	int        sdlState = SDL_PRESSED;
	int        i;
	int        tmp[16];

    SCREEN_API(screen_get_event_property_pv(event, SCREEN_PROPERTY_DEVICE, (void**)&device), "SCREEN_PROPERTY_DEVICE");

    for (i = 0; i < MAX_CONTROLLERS; ++i) {
        if (device == gGameController[i].handle) {
            controller = &gGameController[i];
            break;
        }
    }

    if (!controller) {
        return;
    }

    // Store the controller's new state.
    SCREEN_API(screen_get_event_property_iv(event, SCREEN_PROPERTY_BUTTONS, &controller->buttons), "SCREEN_PROPERTY_BUTTONS");

    if (controller->analogCount > 0) {
    	SCREEN_API(screen_get_event_property_iv(event, SCREEN_PROPERTY_ANALOG0, controller->analog0), "SCREEN_PROPERTY_ANALOG0");
    }

    if (controller->analogCount == 2) {
        SCREEN_API(screen_get_event_property_iv(event, SCREEN_PROPERTY_ANALOG1, controller->analog1), "SCREEN_PROPERTY_ANALOG1");
    }

//    enum {                                                   iPega
//    	SCREEN_A_GAME_BUTTON                   = (1 << 0),
//    	SCREEN_B_GAME_BUTTON                   = (1 << 1),      X
//    	SCREEN_C_GAME_BUTTON                   = (1 << 2),      A
//    	SCREEN_X_GAME_BUTTON                   = (1 << 3),      B
//    	SCREEN_Y_GAME_BUTTON                   = (1 << 4),      Y
//    	SCREEN_Z_GAME_BUTTON                   = (1 << 5),      L1
//    	SCREEN_MENU1_GAME_BUTTON               = (1 << 6),      R1
//    	SCREEN_MENU2_GAME_BUTTON               = (1 << 7),
//    	SCREEN_MENU3_GAME_BUTTON               = (1 << 8),
//    	SCREEN_MENU4_GAME_BUTTON               = (1 << 9),      Select
//    	SCREEN_L1_GAME_BUTTON                  = (1 << 10),     Start
//    	SCREEN_L2_GAME_BUTTON                  = (1 << 11),
//    	SCREEN_L3_GAME_BUTTON                  = (1 << 12),
//    	SCREEN_R1_GAME_BUTTON                  = (1 << 13),
//    	SCREEN_R2_GAME_BUTTON                  = (1 << 14),
//    	SCREEN_R3_GAME_BUTTON                  = (1 << 15),
//    	SCREEN_DPAD_UP_GAME_BUTTON             = (1 << 16),
//    	SCREEN_DPAD_DOWN_GAME_BUTTON           = (1 << 17),
//    	SCREEN_DPAD_LEFT_GAME_BUTTON           = (1 << 18),
//    	SCREEN_DPAD_RIGHT_GAME_BUTTON          = (1 << 19),
//    };

    memset(tmp, 0, sizeof(tmp));
    if (controller->buttons & GAME_DPAD_MASK)
    {
    	if (controller->buttons & SCREEN_DPAD_UP_GAME_BUTTON)
    	{
    		tmp[0] = 1;
    	}
    	if (controller->buttons & SCREEN_DPAD_DOWN_GAME_BUTTON)
    	{
    		tmp[1] = 1;
    	}
    	if (controller->buttons & SCREEN_DPAD_RIGHT_GAME_BUTTON)
    	{
    		tmp[2] = 1;
    	}
    	if (controller->buttons & SCREEN_DPAD_LEFT_GAME_BUTTON)
    	{
    		tmp[3] = 1;
    	}
    }

    // handle DPAD for both PRESS & RELEASE logic
	for (i=0; i<4; i++)
	{
		if (controller->dpadPressState[i] != tmp[i])
		{
			if (tmp[i])
			{
				sdlState = SDL_PRESSED;
			}
			else
			{
				sdlState = SDL_RELEASED;
			}
			keysym.sym      = symcodes[i];
			keysym.scancode = scancodes[i];
			SDL_PrivateKeyboard(sdlState, &keysym);
			controller->dpadPressState[i] = tmp[i];
		}
	}


	memset(tmp, 0, sizeof(tmp));
    if (controller->buttons & (GAME_BUTTON_MASK | GAME_MENU_MASK) )
    {
    	if (controller->bReMap == true)
    	{
    		//    enum {                                                   iPega
    		//    	SCREEN_A_GAME_BUTTON                   = (1 << 0),
    		//    	SCREEN_B_GAME_BUTTON                   = (1 << 1),      X
    		//    	SCREEN_C_GAME_BUTTON                   = (1 << 2),      A
    		//    	SCREEN_X_GAME_BUTTON                   = (1 << 3),      B
    		//    	SCREEN_Y_GAME_BUTTON                   = (1 << 4),      Y
    		//    	SCREEN_Z_GAME_BUTTON                   = (1 << 5),      L1
    		//    	SCREEN_MENU1_GAME_BUTTON               = (1 << 6),      R1
    		//    	SCREEN_MENU2_GAME_BUTTON               = (1 << 7),
    		//    	SCREEN_MENU3_GAME_BUTTON               = (1 << 8),
    		//    	SCREEN_MENU4_GAME_BUTTON               = (1 << 9),      Select
    		//    	SCREEN_L1_GAME_BUTTON                  = (1 << 10),     Start
    		//    	SCREEN_L2_GAME_BUTTON                  = (1 << 11),
    		//    	SCREEN_L3_GAME_BUTTON                  = (1 << 12),
    		//    	SCREEN_R1_GAME_BUTTON                  = (1 << 13),
    		//    	SCREEN_R2_GAME_BUTTON                  = (1 << 14),
    		//    	SCREEN_R3_GAME_BUTTON                  = (1 << 15),
    		int buttons = 0;

    		if (controller->buttons & SCREEN_B_GAME_BUTTON)      buttons |= SCREEN_X_GAME_BUTTON;
    		if (controller->buttons & SCREEN_C_GAME_BUTTON)      buttons |= SCREEN_Y_GAME_BUTTON;
    		if (controller->buttons & SCREEN_Y_GAME_BUTTON)      buttons |= SCREEN_Y_GAME_BUTTON;
    		if (controller->buttons & SCREEN_X_GAME_BUTTON)      buttons |= SCREEN_X_GAME_BUTTON;
    		if (controller->buttons & SCREEN_Z_GAME_BUTTON)      buttons |= SCREEN_L1_GAME_BUTTON;
    		if (controller->buttons & SCREEN_MENU1_GAME_BUTTON)  buttons |= SCREEN_R1_GAME_BUTTON;
    		if (controller->buttons & SCREEN_MENU4_GAME_BUTTON)  buttons |= SCREEN_MENU1_GAME_BUTTON;
    		if (controller->buttons & SCREEN_L1_GAME_BUTTON)     buttons |= SCREEN_MENU2_GAME_BUTTON;

    		controller->buttons = buttons;
    	}

        // TODO: need to support generic Button to SDL control mapping from sdl-control.xml
    	//       only map X, Y, A, B, START, RESET for now (Hard coded)
    	if (controller->buttons & SCREEN_X_GAME_BUTTON)
    	{
    		tmp[0] = SDLK_m;  // Button II
    	}
    	if (controller->buttons & SCREEN_Y_GAME_BUTTON)
    	{
    		tmp[1] = SDLK_l;   // Button I
    	}
    	if (controller->buttons & SCREEN_MENU1_GAME_BUTTON)
    	{
    		tmp[2] = SDLK_RETURN; // MENU 1, "-" button on wiimote
    	}
    	if (controller->buttons & SCREEN_MENU2_GAME_BUTTON)
    	{
    		tmp[3] = SDLK_SPACE; // MENU 2, "+" button on wiimote
    	}
    	if (controller->buttons & SCREEN_MENU3_GAME_BUTTON)
    	{
    		tmp[4] = SDLK_0;      // MENU 3, HOME button on wiimote
    	}
    	if (controller->buttons & (SCREEN_B_GAME_BUTTON | SCREEN_L1_GAME_BUTTON) )
    	{
    		tmp[5] = SDLK_q;      // Button B of Wiimote, Simulate L
    	}
    	if (controller->buttons & (SCREEN_A_GAME_BUTTON | SCREEN_R1_GAME_BUTTON) )
    	{
    		tmp[6] = SDLK_p;      // Button A of Wiimote, Simulate R
    	}
    	// Combo key R+START (in wiimote, "A"+"-"), Backup game state
    	if ( (controller->buttons & (SCREEN_A_GAME_BUTTON | SCREEN_MENU1_GAME_BUTTON)) == (SCREEN_A_GAME_BUTTON | SCREEN_MENU1_GAME_BUTTON) )
    	{
    		tmp[2] = tmp[6] = 0;  // Clear original button state
    		tmp[7] = SDLK_F8;     // Backup game state
    	}
    	// Combo key R+SELECT (in wiimote, "A"+"+"), Restore game state
    	if ( (controller->buttons & (SCREEN_A_GAME_BUTTON | SCREEN_MENU2_GAME_BUTTON)) == (SCREEN_A_GAME_BUTTON | SCREEN_MENU2_GAME_BUTTON) )
    	{
    		tmp[3] = tmp[6] = 0;  // Clear original button state
    		tmp[8] = SDLK_F9;     // Restore game state
    	}
    }

	for (i=0; i<9; i++)
	{
		if (controller->buttonPressState[i] != tmp[i])
		{
			if (tmp[i])
			{
				sdlState = SDL_PRESSED;
				controller->buttonPressState[i] = tmp[i];
			}
			else
			{
				sdlState = SDL_RELEASED;
				tmp[i] = controller->buttonPressState[i];
				controller->buttonPressState[i] = 0;
			}

			keysym.sym      = tmp[i];
			keysym.mod      = KMOD_NONE;
			keysym.scancode = tmp[i];
			keysym.unicode  = 0;
			SDL_PrivateKeyboard(sdlState, &keysym);
		}
	}

}

static void handleDeviceEvent(screen_event_t event)
{
    // A device was attached or removed.
    screen_device_t device;
    int attached;
    int type;
    int i;

    SCREEN_API(screen_get_event_property_pv(event, SCREEN_PROPERTY_DEVICE, (void**)&device), "SCREEN_PROPERTY_DEVICE");
    SCREEN_API(screen_get_event_property_iv(event, SCREEN_PROPERTY_ATTACHED, &attached), "SCREEN_PROPERTY_ATTACHED");

    if (attached) {
        SCREEN_API(screen_get_device_property_iv(device, SCREEN_PROPERTY_TYPE, &type), "SCREEN_PROPERTY_TYPE");
    }


    if (attached && (type == SCREEN_EVENT_GAMEPAD || type == SCREEN_EVENT_JOYSTICK)) {
        for (i = 0; i < MAX_CONTROLLERS; ++i) {
            if (!gGameController[i].handle) {
            	gGameController[i].handle = device;
                loadController(&gGameController[i]);
                break;
            }
        }
    } else {
        for (i = 0; i < MAX_CONTROLLERS; ++i) {
            if (device == gGameController[i].handle) {
                memset(&gGameController[i], 0, sizeof(GameController_t));
                break;
            }
        }
    }
}
#endif

static void handlePointerEvent(screen_event_t event, screen_window_t window)
{
	int buttonState = 0;
	screen_get_event_property_iv(event, SCREEN_PROPERTY_BUTTONS, &buttonState);

	int coords[2];
	screen_get_event_property_iv(event, SCREEN_PROPERTY_SOURCE_POSITION, coords);

	int screen_coords[2];
	screen_get_event_property_iv(event, SCREEN_PROPERTY_POSITION, screen_coords);

	int wheel_delta;
	screen_get_event_property_iv(event, SCREEN_PROPERTY_MOUSE_WHEEL, &wheel_delta);

	if (coords[1] < 0) {
		fprintf(stderr, "Detected pointer swipe event: %d,%d\n", coords[0], coords[1]);
		return;
	}
	//fprintf(stderr, "Pointer: %d,(%d,%d),(%d,%d),%d\n", buttonState, coords[0], coords[1], screen_coords[0], screen_coords[1], wheel_delta);
	if (wheel_delta != 0) {
		int button;
		if ( wheel_delta > 0 )
			button = SDL_BUTTON_WHEELDOWN;
		else if ( wheel_delta < 0 )
			button = SDL_BUTTON_WHEELUP;
		SDL_PrivateMouseButton(
			SDL_PRESSED, button, 0, 0);
		SDL_PrivateMouseButton(
			SDL_RELEASED, button, 0, 0);
	}

	// FIXME: Pointer events have never been tested.
	static int lastButtonState = 0;
	if (lastButtonState == buttonState) {
		moveEvent.touching = buttonState;
		moveEvent.pos[0] = coords[0];
		moveEvent.pos[1] = coords[1];
		moveEvent.pending = 1;
		return;
	}
	lastButtonState = buttonState;

	SDL_PrivateMouseButton(buttonState ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_LEFT, coords[0], coords[1]); // FIXME: window
	moveEvent.pending = 0;
}

static int TranslateBluetoothKeyboard(int sym, int mods, int flags, int scan, int cap, SDL_keysym *keysym)
{
	if (flags == 32) {
		return 0; // No translation for this - this is an addition message sent
		// with arrow keys, right ctrl, right ctrl and pause
	}
	// FIXME: Figure out how to separate left and right modifiers
	if (scan > 128)
		scan -= 128; // Keyup events have the high bit set, but we want to have the same scan for down and up.
	keysym->scancode = scan;
	keysym->mod = 0;
	if (mods & (0x1))
		keysym->mod |= KMOD_LSHIFT;
	if (mods & (0x2))
		keysym->mod |= KMOD_LCTRL;
	if (mods & (0x4))
		keysym->mod |= KMOD_LALT;
	if (mods & (0x10000))
		keysym->mod |= KMOD_CAPS;
	if (mods & (0x20000)) // FIXME: guessing
		keysym->mod |= KMOD_NUM;
	//if (mods & (0x40000))
		//keysym.mod |= SCROLL LOCK; // SDL has no scroll lock

	if (sym & 0xf000) {
		sym = sym & 0xff;
		keysym->sym = Playbook_specialsyms[sym];
	} else {
		keysym->sym = Playbook_Keycodes[sym].sym;
	}

	return 1;
}

static int TranslateVKB(int sym, int mods, int flags, int scan, int cap, SDL_keysym *keysym)
{
	int shifted = 0;
	// FIXME: Keyboard handling (modifiers are currently ignored, some keys are as well)
	if (sym & 0xf000) {
		sym = sym & 0xff;
		keysym->sym = Playbook_specialsyms[sym];
	} else {
		keysym->sym = Playbook_Keycodes[sym].sym;
	}

	if (mods & (0x1))
		shifted = 1;

	// FIXME: These scancodes are really just implemented this way for dosbox.
	// See keyboard.cpp inside dosbox (KEYBOARD_AddKey) for a reference.
	switch (keysym->sym)
	{
	case SDLK_EXCLAIM:
		shifted = 1;
	case SDLK_1:
		keysym->scancode = 2;
		break;
	case SDLK_AT:
		shifted = 1;
	case SDLK_2:
		keysym->scancode = 3;
		break;
	case SDLK_HASH:
		shifted = 1;
	case SDLK_3:
		keysym->scancode = 4;
		break;
	case SDLK_DOLLAR:
		shifted = 1;
	case SDLK_4:
		keysym->scancode = 5;
		break;
	case SDLK_5:
		keysym->scancode = 6;
		break;
	case SDLK_CARET:
		shifted = 1;
	case SDLK_6:
		keysym->scancode = 7;
		break;
	case SDLK_AMPERSAND:
		shifted = 1;
	case SDLK_7:
		keysym->scancode = 8;
		break;
	case SDLK_ASTERISK:
		shifted = 1;
	case SDLK_8:
		keysym->scancode = 9;
		break;
	case SDLK_LEFTPAREN:
		shifted = 1;
	case SDLK_9:
		keysym->scancode = 10;
		break;
	case SDLK_RIGHTPAREN:
		shifted = 1;
	case SDLK_0:
		keysym->scancode = 11;
		break;
	case SDLK_UNDERSCORE:
		shifted = 1;
	case SDLK_MINUS:
		keysym->scancode = 12;
		break;
	case SDLK_PLUS:
		shifted = 1;
	case SDLK_EQUALS:
		keysym->scancode = 13;
		break;
	case SDLK_BACKSPACE:
		keysym->scancode = 14;
		break;
	case SDLK_TAB:
		keysym->scancode = 15;
		break;
	case SDLK_q:
		keysym->scancode = 16;
		break;
	case SDLK_w:
		keysym->scancode = 17;
		break;
	case SDLK_e:
		keysym->scancode = 18;
		break;
	case SDLK_r:
		keysym->scancode = 19;
		break;
	case SDLK_t:
		keysym->scancode = 20;
		break;
	case SDLK_y:
		keysym->scancode = 21;
		break;
	case SDLK_u:
		keysym->scancode = 22;
		break;
	case SDLK_i:
		keysym->scancode = 23;
		break;
	case SDLK_o:
		keysym->scancode = 24;
		break;
	case SDLK_p:
		keysym->scancode = 25;
		break;
	case SDLK_LEFTBRACKET:
		keysym->scancode = 26;
		break;
	case SDLK_RIGHTBRACKET:
		keysym->scancode = 27;
		break;
	case SDLK_RETURN:
		keysym->scancode = 28;
		break;
	case SDLK_a:
		keysym->scancode = 30;
		break;
	case SDLK_s:
		keysym->scancode = 31;
		break;
	case SDLK_d:
		keysym->scancode = 32;
		break;
	case SDLK_f:
		keysym->scancode = 33;
		break;
	case SDLK_g:
		keysym->scancode = 34;
		break;
	case SDLK_h:
		keysym->scancode = 35;
		break;
	case SDLK_j:
		keysym->scancode = 36;
		break;
	case SDLK_k:
		keysym->scancode = 37;
		break;
	case SDLK_l:
		keysym->scancode = 38;
		break;
	case SDLK_COLON:
		shifted = 1;
	case SDLK_SEMICOLON:
		keysym->scancode = 39;
		break;
	case SDLK_QUOTEDBL:
		shifted = 1;
	case SDLK_QUOTE:
		keysym->scancode = 40;
		break;
	case SDLK_PB_TILDE:
		shifted = 1;
	case SDLK_BACKQUOTE:
		keysym->scancode = 41;
		break;
	case SDLK_BACKSLASH:
		keysym->scancode = 43;
		break;
	case SDLK_z:
		keysym->scancode = 44;
		break;
	case SDLK_x:
		keysym->scancode = 45;
		break;
	case SDLK_c:
		keysym->scancode = 46;
		break;
	case SDLK_v:
		keysym->scancode = 47;
		break;
	case SDLK_b:
		keysym->scancode = 48;
		break;
	case SDLK_n:
		keysym->scancode = 49;
		break;
	case SDLK_m:
		keysym->scancode = 50;
		break;
	case SDLK_LESS:
		shifted = 1;
	case SDLK_COMMA:
		keysym->scancode = 51;
		break;
	case SDLK_GREATER:
		shifted = 1;
	case SDLK_PERIOD:
		keysym->scancode = 52;
		break;
	case SDLK_QUESTION:
		shifted = 1;
	case SDLK_SLASH:
		keysym->scancode = 53;
		break;
	case SDLK_SPACE:
		keysym->scancode = 57;
		break;
	}

	switch (keysym->sym)
	{
	case SDLK_a:
		keysym->unicode = 'a';
		break;
	case SDLK_b:
		keysym->unicode = 'b';
		break;
	case SDLK_c:
		keysym->unicode = 'c';
		break;
	case SDLK_d:
		keysym->unicode = 'd';
		break;
	case SDLK_e:
		keysym->unicode = 'e';
		break;
	case SDLK_f:
		keysym->unicode = 'f';
		break;
	case SDLK_g:
		keysym->unicode = 'g';
		break;
	case SDLK_h:
		keysym->unicode = 'h';
		break;
	case SDLK_i:
		keysym->unicode = 'i';
		break;
	case SDLK_j:
		keysym->unicode = 'j';
		break;
	case SDLK_k:
		keysym->unicode = 'k';
		break;
	case SDLK_l:
		keysym->unicode = 'l';
		break;
	case SDLK_m:
		keysym->unicode = 'm';
		break;
	case SDLK_n:
		keysym->unicode = 'n';
		break;
	case SDLK_o:
		keysym->unicode = 'o';
		break;
	case SDLK_p:
		keysym->unicode = 'p';
		break;
	case SDLK_q:
		keysym->unicode = 'q';
		break;
	case SDLK_r:
		keysym->unicode = 'r';
		break;
	case SDLK_s:
		keysym->unicode = 's';
		break;
	case SDLK_t:
		keysym->unicode = 't';
		break;
	case SDLK_u:
		keysym->unicode = 'u';
		break;
	case SDLK_v:
		keysym->unicode = 'v';
		break;
	case SDLK_w:
		keysym->unicode = 'w';
		break;
	case SDLK_x:
		keysym->unicode = 'x';
		break;
	case SDLK_y:
		keysym->unicode = 'y';
		break;
	case SDLK_z:
		keysym->unicode = 'z';
		break;
	case SDLK_1:
		keysym->unicode = '1';
		break;
	case SDLK_2:
		keysym->unicode = '2';
		break;
	case SDLK_3:
		keysym->unicode = '3';
		break;
	case SDLK_4:
		keysym->unicode = '4';
		break;
	case SDLK_5:
		keysym->unicode = '5';
		break;
	case SDLK_6:
		keysym->unicode = '6';
		break;
	case SDLK_7:
		keysym->unicode = '7';
		break;
	case SDLK_8:
		keysym->unicode = '8';
		break;
	case SDLK_9:
		keysym->unicode = '9';
		break;
	case SDLK_0:
		keysym->unicode = '0';
		break;
	case SDLK_COMMA:
		keysym->unicode = ',';
		break;
	case SDLK_PERIOD:
		keysym->unicode = '.';
		break;
	case SDLK_COLON:
		keysym->unicode = ':';
		break;
	case SDLK_SLASH:
		keysym->unicode = '/';
		break;
	case SDLK_BACKSPACE:
		keysym->unicode = SDLK_BACKSPACE;
		break;
	case SDLK_DELETE:
		keysym->unicode = SDLK_DELETE;
		break;
	case SDLK_ESCAPE:
		keysym->unicode = SDLK_ESCAPE;
		break;
	case SDLK_RETURN:
		keysym->unicode = SDLK_RETURN;
		break;
	case SDLK_DOLLAR:
		keysym->unicode = '$';
		break;
	case SDLK_SPACE:
		keysym->unicode = ' ';
		break;
	case SDLK_MINUS:
		keysym->unicode = '-';
		break;
	case SDLK_EXCLAIM:
		keysym->unicode = '!';
		break;
	case SDLK_QUESTION:
		keysym->unicode = '?';
		break;
	case SDLK_HASH:
		keysym->unicode = '#';
		break;
	case SDLK_AT:
		keysym->unicode = '@';
		break;
	case SDLK_ASTERISK:
		keysym->unicode = '*';
		break;
	case SDLK_LEFTPAREN:
		keysym->unicode = '(';
		break;
	case SDLK_RIGHTPAREN:
		keysym->unicode = ')';
		break;
	case SDLK_EQUALS:
		keysym->unicode = '=';
		break;
	case SDLK_PLUS:
		keysym->unicode = '+';
		break;
	case SDLK_LESS:
		keysym->unicode = '<';
		break;
	case SDLK_GREATER:
		keysym->unicode = '>';
		break;
	case SDLK_SEMICOLON:
		keysym->unicode = ';';
		break;
	case SDLK_QUOTEDBL:
		keysym->unicode = '"';
		break;
	case SDLK_QUOTE:
		keysym->unicode = '\'';
		break;
	}
	keysym->mod = KMOD_NONE;
	return shifted;
}

static void handleKeyboardEvent(screen_event_t event)
{
	static const int KEYBOARD_TYPE_MASK = 0x20;
    int sym = 0;
    screen_get_event_property_iv(event, SCREEN_PROPERTY_KEY_SYM, &sym);
    int modifiers = 0;
    screen_get_event_property_iv(event, SCREEN_PROPERTY_KEY_MODIFIERS, &modifiers);
    int flags = 0;
    screen_get_event_property_iv(event, SCREEN_PROPERTY_KEY_FLAGS, &flags);
    int scan = 0;
    screen_get_event_property_iv(event, SCREEN_PROPERTY_KEY_SCAN, &scan);
    int cap = 0;
    screen_get_event_property_iv(event, SCREEN_PROPERTY_KEY_CAP, &cap);

	int shifted = 0;
	SDL_keysym keysym;
    if (flags & KEYBOARD_TYPE_MASK) {
    	if (!TranslateBluetoothKeyboard(sym, modifiers, flags, scan, cap, &keysym))
    	{
    		return; // No translation
    	}
    } else {
		shifted = TranslateVKB(sym, modifiers, flags, scan, cap, &keysym);
    }

    if (shifted) {
		SDL_keysym temp;
		temp.scancode = 42;
		temp.sym = SDLK_LSHIFT;
		SDL_PrivateKeyboard(SDL_PRESSED, &temp);
    }

    SDL_PrivateKeyboard((flags & 0x1)?SDL_PRESSED:SDL_RELEASED, &keysym);

    if (shifted) {
		SDL_keysym temp;
		temp.scancode = 42;
		temp.sym = SDLK_LSHIFT;
		SDL_PrivateKeyboard(SDL_RELEASED, &temp);
    }
}

static void handleMtouchEvent(screen_event_t event, screen_window_t window, int type)
{
#ifdef TOUCHPAD_SIMULATE

	unsigned buttonWidth = 200;
	unsigned buttonHeight = 300;

	int contactId;
	int pos[2];
	int screenPos[2];
	screen_get_event_property_iv(event, SCREEN_PROPERTY_TOUCH_ID, (int*)&contactId);
	screen_get_event_property_iv(event, SCREEN_PROPERTY_SOURCE_POSITION, pos);
	screen_get_event_property_iv(event, SCREEN_PROPERTY_POSITION, screenPos);

	int mouseX, mouseY;
	SDL_GetMouseState(&mouseX, &mouseY);

	if (state.leftId == contactId) {
		if (type == SCREEN_EVENT_MTOUCH_RELEASE) {
			state.leftId = -1;
			SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_LEFT, mouseX, mouseY);
		}
		return;
	}
	if (state.rightId == contactId) {
		if (type == SCREEN_EVENT_MTOUCH_RELEASE) {
			state.rightId = -1;
			SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_RIGHT, mouseX, mouseY);
		}
		return;
	}

	if (screenPos[0] < buttonWidth && contactId != state.moveId) {
		// Special handling for buttons

		if (screenPos[1] < buttonHeight) {
			// Left button
			if (type == SCREEN_EVENT_MTOUCH_TOUCH) {
				if (state.leftId == -1 && contactId != state.rightId) {
					SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_LEFT, mouseX, mouseY);
					state.leftId = contactId;
				}
			}
		} else {
			// Right button
			if (type == SCREEN_EVENT_MTOUCH_TOUCH) {
				if (state.rightId == -1 && contactId != state.leftId) {
					SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_RIGHT, mouseX, mouseY);
					state.rightId = contactId;
				}
			}
		}
	} else {
		// Can only generate motion events
		int doMove = 0;
		switch (type) {
		case SCREEN_EVENT_MTOUCH_TOUCH:
			if (state.moveId == -1 || state.moveId == contactId) {
				state.moveId = contactId;
			}
			break;
		case SCREEN_EVENT_MTOUCH_RELEASE:
			if (state.moveId == contactId) {
				doMove = 1;
				state.moveId = -1;
			}
			break;
		case SCREEN_EVENT_MTOUCH_MOVE:
			if (state.moveId == contactId) {
				doMove = 1;
			}
			break;
		}

		if (doMove) {
			int mask = 0;
			if (state.leftDown)
				mask |= SDL_BUTTON_LEFT;
			if (state.rightDown)
				mask |= SDL_BUTTON_RIGHT;
			state.mask = mask;
			state.pending[0] += pos[0] - state.oldPos[0];
			state.pending[1] += pos[1] - state.oldPos[1];
			//SDL_PrivateMouseMotion(mask, 1, pos[0] - state.oldPos[0], pos[1] - state.oldPos[1]);
		}
		state.oldPos[0] = pos[0];
		state.oldPos[1] = pos[1];
	}
#else
    int contactId;
    int pos[2];
    int screenPos[2];
    int orientation;
    int pressure;
    long long timestamp;
    int sequenceId;

    screen_get_event_property_iv(event, SCREEN_PROPERTY_TOUCH_ID, (int*)&contactId);
    screen_get_event_property_iv(event, SCREEN_PROPERTY_SOURCE_POSITION, pos);
    screen_get_event_property_iv(event, SCREEN_PROPERTY_POSITION, screenPos);
    screen_get_event_property_iv(event, SCREEN_PROPERTY_TOUCH_ORIENTATION, (int*)&orientation);
    screen_get_event_property_iv(event, SCREEN_PROPERTY_TOUCH_PRESSURE, (int*)&pressure); // Pointless - always 1 if down
    screen_get_event_property_llv(event, SCREEN_PROPERTY_TIMESTAMP, (long long*)&timestamp);
    screen_get_event_property_iv(event, SCREEN_PROPERTY_SEQUENCE_ID, (int*)&sequenceId);

//    char typeChar = 'D';
//    if (type == SCREEN_EVENT_MTOUCH_RELEASE)
//    	typeChar = 'U';
//    else if (type == SCREEN_EVENT_MTOUCH_MOVE)
//    	typeChar = 'M';
//
//    fprintf(stderr, "Touch %d: (%d,%d) %c\n", contactId, pos[0], pos[1], typeChar);

    if (pos[1] < 0) {
    	fprintf(stderr, "Detected swipe event: %d,%d\n", pos[0], pos[1]);
    	return;
    }
    if (type == SCREEN_EVENT_MTOUCH_TOUCH) {
        SDL_PrivateMultiMouseButton(contactId, SDL_PRESSED, SDL_BUTTON_LEFT, pos[0], pos[1]);
    } else if (type == SCREEN_EVENT_MTOUCH_RELEASE) {
        SDL_PrivateMultiMouseButton(contactId, SDL_RELEASED, SDL_BUTTON_LEFT, pos[0], pos[1]);
    } else if (type == SCREEN_EVENT_MTOUCH_MOVE) {
        SDL_PrivateMultiMouseMotion(contactId, SDL_BUTTON_LEFT, 0, pos[0], pos[1]);
    }

    // TODO: Possibly need more complicated touch handling
#endif
}

void handleCustomEvent(_THIS, bps_event_t *event)
{
	SDL_SysWMmsg wmmsg;
	SDL_VERSION(&wmmsg.version);
	wmmsg.event = event;
	SDL_PrivateSysWMEvent(&wmmsg);
}

int handleTopMenuKeyFunc(int sym, int mod, int scancode, uint16_t unicode, int event)
{
	SLOG("sym: %d, event: %d", sym, event);

	int sdlEvent, sdlEvent2;
	switch (event)
	{
	case TCO_KB_DOWN:
		sdlEvent = SDL_PRESSED;
		sdlEvent2= SDL_RELEASED;
		break;
	case TCO_KB_UP:
		sdlEvent = SDL_RELEASED;
		sdlEvent2= SDL_RELEASED;
		break;
	default:
		return TCO_UNHANDLED;
	}

	SDL_keysym keysym;
	keysym.sym = sym;
	keysym.mod = mod;
	keysym.scancode = scancode;
	keysym.unicode = unicode;

	SDL_PrivateKeyboard(sdlEvent, &keysym);
	SDL_PrivateKeyboard(sdlEvent2, &keysym);

	// Button Config
	if (sym == SDLK_MINUS)
	{
		bShowConfigWindow = 1;
	}
	return TCO_SUCCESS;
}


struct tco_callbacks cb = {handleTopMenuKeyFunc, NULL, NULL, NULL, NULL, NULL};


#define TOP_MENU_WIDTH  480
#define TOP_MENU_HEIGHT 150

void topMenuInit(_THIS)
{
	screen_buffer_t screen_buf[2];
	int             rect[4] = { 0, 0, TOP_MENU_WIDTH, TOP_MENU_HEIGHT };
	int             pos[2];
    const int       usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE | SCREEN_USAGE_READ;
    tco_context_t   tco_ctx;
    int             zorder;
    int             mainZorder;
	char            groupName[256];
	int             format = SCREEN_FORMAT_RGBA8888;
	int             idle_mode = getenv("SCREEN_IDLE_NORMAL") != NULL ? SCREEN_IDLE_MODE_NORMAL : SCREEN_IDLE_MODE_KEEP_AWAKE;
    int             i, j, rc;
    int             stride;
    unsigned char  *pixels;

    bShowConfigWindow = 0;

    screen_get_window_property_iv(_priv->screenWindow, SCREEN_PROPERTY_ZORDER, &mainZorder);
    SLOG("Main window Zorder: %d", mainZorder);

    /* Setup the window */
	screen_create_window_type(&_priv->topMenuWindow, _priv->screenContext, SCREEN_CHILD_WINDOW);
	snprintf(groupName, 256, "SDL-window-%d", getpid());
	screen_join_window_group(_priv->topMenuWindow, groupName);
	screen_set_window_property_iv(_priv->topMenuWindow, SCREEN_PROPERTY_IDLE_MODE, &idle_mode);
	PLAYBOOK_SetupStretch(this, _priv->topMenuWindow, rect[2], rect[3], SDL_RESIZABLE);
    screen_get_window_property_iv(_priv->topMenuWindow, SCREEN_PROPERTY_POSITION, pos);
	screen_set_window_property_iv(_priv->topMenuWindow, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(_priv->topMenuWindow, SCREEN_PROPERTY_USAGE,  &usage);
	screen_create_window_buffers(_priv->topMenuWindow, 1);

	zorder = mainZorder + 2;
    screen_set_window_property_iv(_priv->topMenuWindow, SCREEN_PROPERTY_ZORDER, &zorder);
    screen_get_window_property_pv(_priv->topMenuWindow, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)screen_buf);
    screen_get_buffer_property_pv(screen_buf[0], SCREEN_PROPERTY_POINTER, (void **)&pixels);
    screen_get_buffer_property_iv(screen_buf[0], SCREEN_PROPERTY_STRIDE, &stride);

	// Fill ConfigWindow background color
    unsigned char blue=0xa0;
	for (i=0; i<rect[3]; i++) {
		for (j=0; j<rect[2]; j++) {
			pixels[i*stride+j*4]   = 0xFF;
			pixels[i*stride+j*4+1] = 0x20;
			pixels[i*stride+j*4+2] = 0x20;
			pixels[i*stride+j*4+3] = blue;
		}
		blue -= 1;
	}

	// show animation of the Top menu, slide up from middle of screen
	while (pos[1] > rect[3])
	{
		pos[1]-=3;
		screen_set_window_property_iv(_priv->topMenuWindow, SCREEN_PROPERTY_POSITION, pos);
	    screen_post_window(_priv->topMenuWindow, screen_buf[0], 1, rect, 0);
	}

	// Init Touch overlay module
    rc = tco_initialize(&tco_ctx, _priv->screenContext, cb);
    if(rc != TCO_SUCCESS)
    {
    	SLOG("TCO top menu: Init error");
    	return;
    }

	// Load Button controls from file
	char cwd[256];
	if ((getcwd(cwd, 256) != NULL) && (chdir(_priv->tcoControlsDir) == 0))
    {
    	SLOG("loading sdl-topmenu-controls.xml");
    	rc = tco_loadcontrols(tco_ctx, "sdl-topmenu-controls.xml");
		chdir(cwd);

    	if (rc != TCO_SUCCESS)
        {
        	SLOG("TCO top menu: Load Controls Error");
        	return;
        }
    }

    tco_showlabels(tco_ctx, _priv->topMenuWindow);
    tco_adjustAlpha(tco_ctx, 0xFF); // opaque button

	bps_event_t *event;
	int          bEventLoop = 1;
	int          domain;

	// Wait for touch event
	while (bEventLoop)
	{
		bps_get_event(&event, 0);

		if (event)
		{
			domain = bps_event_get_domain(event);
			if (domain == navigator_get_domain())
			{
				switch (bps_event_get_code(event))
				{
					case NAVIGATOR_EXIT:
						SDL_PrivateQuit(); // We can't stop it from closing anyway
						break;
					case NAVIGATOR_WINDOW_STATE:
					{
						navigator_window_state_t state = navigator_event_get_window_state(event);
						switch (state) {
						case NAVIGATOR_WINDOW_FULLSCREEN:
							SDL_PrivateAppActive(1, (SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS));
							break;
						case NAVIGATOR_WINDOW_THUMBNAIL:
							SDL_PrivateAppActive(0, (SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS));
							break;
						case NAVIGATOR_WINDOW_INVISIBLE:
							SDL_PrivateAppActive(0, (SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS));
							break;
						}
					}
						break;
					case NAVIGATOR_SWIPE_DOWN:
					{
						// Destroy menu and return
						SLOG("Swipe down: Destroying top menu");

						tco_shutdown(tco_ctx);

						screen_destroy_window(_priv->topMenuWindow);

						_priv->topMenuWindow = NULL;

						bEventLoop = 0;
						break;
					}
				}
			}
			else if (domain == screen_get_domain())
			{
				int            type;
				screen_event_t se = screen_event_get_event(event);
				int            rc = screen_get_event_property_iv(se, SCREEN_PROPERTY_TYPE, &type);

				if (rc || type == SCREEN_EVENT_NONE)
					break;

				switch (type)
				{
					case SCREEN_EVENT_MTOUCH_TOUCH:
					case SCREEN_EVENT_MTOUCH_MOVE:
					case SCREEN_EVENT_MTOUCH_RELEASE:
						if (TCO_SUCCESS == tco_touch(tco_ctx, se))
						{
							// Destroy menu and return
							SLOG("Top Menu button selected: Destroying top menu");

							tco_shutdown(tco_ctx);

							screen_destroy_window(_priv->topMenuWindow);

							_priv->topMenuWindow = NULL;

							bEventLoop = 0;
						}
						break;
				}

			}
		}

		if (_priv->topMenuWindow)
		{
			screen_post_window(_priv->topMenuWindow, screen_buf[0], 1, rect, 0);
		}
	}

    zorder = mainZorder;
    screen_set_window_property_iv(_priv->screenWindow, SCREEN_PROPERTY_ZORDER, &zorder);
}


void handleNavigatorEvent(_THIS, bps_event_t *event)
{
	int rc, angle;
	switch (bps_event_get_code(event))
	{
	case NAVIGATOR_INVOKE:
		//fprintf(stderr, "Navigator invoke\n");
		break;
	case NAVIGATOR_EXIT:
		SDL_PrivateQuit(); // We can't stop it from closing anyway
		break;
	case NAVIGATOR_WINDOW_STATE:
	{
		navigator_window_state_t state = navigator_event_get_window_state(event);
		switch (state) {
		case NAVIGATOR_WINDOW_FULLSCREEN:
			SDL_PrivateAppActive(1, (SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS));
			//fprintf(stderr, "Fullscreen\n");
			break;
		case NAVIGATOR_WINDOW_THUMBNAIL:
			SDL_PrivateAppActive(0, (SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS));
			//fprintf(stderr, "Thumbnail\n"); // TODO: Consider pausing?
			break;
		case NAVIGATOR_WINDOW_INVISIBLE:
			SDL_PrivateAppActive(0, (SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS));
			//fprintf(stderr, "Invisible\n"); // TODO: Consider pausing?
			break;
		}
	}
		break;
	case NAVIGATOR_SWIPE_DOWN:
	{
		topMenuInit(this);

		if (bShowConfigWindow) {
			tco_swipedown(_priv->emu_context, _priv->screenWindow);
		}
		break;
	}
	case NAVIGATOR_SWIPE_START:
		//fprintf(stderr, "Swipe start\n");
		break;
	case NAVIGATOR_LOW_MEMORY:
		//fprintf(stderr, "Low memory\n"); // TODO: Anything we can do?
		break;
	case NAVIGATOR_ORIENTATION_CHECK:
		//fprintf(stderr, "Orientation check\n");
		navigator_orientation_check_response(event, getenv("AUTO_ORIENTATION") != NULL ? true : false);
		break;
	case NAVIGATOR_ORIENTATION:
		//fprintf(stderr, "Navigator orientation\n");
		angle = navigator_event_get_orientation_angle(event);

		int angle_diff = abs(angle - _priv->angle);
		int newsize[2] = {_priv->w, _priv->h};

		if(angle_diff == 90 || angle_diff == 270){
			/* flip */
			newsize[0] = _priv->h;
			newsize[1] = _priv->w;
		}

		rc  = screen_set_window_property_iv(_priv->mainWindow, SCREEN_PROPERTY_ROTATION, &angle);
		rc |= screen_set_window_property_iv(_priv->mainWindow, SCREEN_PROPERTY_SIZE, newsize);
		rc |= screen_set_window_property_iv(_priv->mainWindow, SCREEN_PROPERTY_SOURCE_SIZE, newsize);
		if(rc){
			SDL_SetError("Could not set mainWindow size or rotation: %s\n", strerror(errno));
		}

		SDL_PrivateResize(newsize[0], newsize[1]);
		navigator_done_orientation(event);
		break;
	case NAVIGATOR_BACK:
		//fprintf(stderr, "Navigator back\n");
		break;
	case NAVIGATOR_WINDOW_ACTIVE:
		//fprintf(stderr, "Window active\n"); // TODO: Handle?
		break;
	case NAVIGATOR_WINDOW_INACTIVE:
		//fprintf(stderr, "Window inactive\n"); // TODO: Handle?
		break;
	default:
		//fprintf(stderr, "Unknown navigator event: %d\n", bps_event_get_code(event));
		break;
	}
}

static void handleDisplayEvent(screen_event_t event, screen_window_t window)
{
	SLOG("Display Event, possible HDMI plug in/out");
}

void handleScreenEvent(_THIS, bps_event_t *event)
{
	int type;
	screen_event_t se = screen_event_get_event(event);
	int rc = screen_get_event_property_iv(se, SCREEN_PROPERTY_TYPE, &type);
	if (rc || type == SCREEN_EVENT_NONE)
		return;

	screen_window_t window;
	screen_get_event_property_pv(se, SCREEN_PROPERTY_WINDOW, (void **)&window);
	if (!window && type != SCREEN_EVENT_KEYBOARD)
		return;

	switch (type)
	{
		case SCREEN_EVENT_CLOSE:
			// Do nothing.
			break;
		case SCREEN_EVENT_PROPERTY:
			{
				int val;
				screen_get_event_property_iv(se, SCREEN_PROPERTY_NAME, &val);

				//fprintf(stderr, "Property change (property val=%d)\n", val);
			}
			break;
		case SCREEN_EVENT_DISPLAY:
			handleDisplayEvent(se, window);
			break;
		case SCREEN_EVENT_POINTER:
			handlePointerEvent(se, window);
			break;
		case SCREEN_EVENT_KEYBOARD:
			handleKeyboardEvent(se);
			break;
		case SCREEN_EVENT_MTOUCH_TOUCH:
		case SCREEN_EVENT_MTOUCH_MOVE:
		case SCREEN_EVENT_MTOUCH_RELEASE:
			if (_priv->tcoControlsDir) {
				tco_touch(this->hidden->emu_context, se);
			} else {
				handleMtouchEvent(se, window, type);
			}
			break;

		case SCREEN_EVENT_DEVICE:
//			handleDeviceEvent(se);
			SDL_JoystickInit();

			// enable SYSTEM specific event reporting
			SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

			handleCustomEvent(this, event);
			break;

        case SCREEN_EVENT_GAMEPAD:
        case SCREEN_EVENT_JOYSTICK:
//        	handleGameControllerEvent(se);
        	SDL_JoystickUpdate();
        	break;
	}
}

void
PLAYBOOK_PumpEvents(_THIS)
{
#if 1
	bps_event_t *event;
	bps_get_event(&event, 0);
	while (event)
	{
		int domain = bps_event_get_domain(event);
		if (domain == navigator_get_domain()) {
			handleNavigatorEvent(this, event);
		}
		else if (domain == screen_get_domain()) {
			handleScreenEvent(this, event);
		}

		// post SDL_SYSWMEVENT event containing the bps event pointer
//		handleCustomEvent(this, event);

		bps_get_event(&event, 0);
	}
#else
	while (1)
	{
		int rc = screen_get_event(this->hidden->screenContext, this->hidden->screenEvent, 0 /*timeout*/);
		if (rc)
			break;

		int type;
		rc = screen_get_event_property_iv(this->hidden->screenEvent, SCREEN_PROPERTY_TYPE, &type);
		if (rc || type == SCREEN_EVENT_NONE)
			break;

		screen_window_t window;
		screen_get_event_property_pv(this->hidden->screenEvent, SCREEN_PROPERTY_WINDOW, (void **)&window);
		if (!window && type != SCREEN_EVENT_KEYBOARD)
			break;

		switch (type)
		{
		case SCREEN_EVENT_CLOSE:
			SDL_PrivateQuit(); // We can't stop it from closing anyway
			break;
		case SCREEN_EVENT_PROPERTY:
			{
				int val;
				screen_get_event_property_iv(this->hidden->screenEvent, SCREEN_PROPERTY_NAME, &val);

				//fprintf(stderr, "Property change (property val=%d)\n", val);
			}
			break;
		case SCREEN_EVENT_POINTER:
			handlePointerEvent(this->hidden->screenEvent, window);
			break;
		case SCREEN_EVENT_KEYBOARD:
			handleKeyboardEvent(this->hidden->screenEvent);
			break;
		case SCREEN_EVENT_MTOUCH_TOUCH:
		case SCREEN_EVENT_MTOUCH_MOVE:
		case SCREEN_EVENT_MTOUCH_RELEASE:
			//handleMtouchEvent(this->hidden->screenEvent, window, type);
			emulate_touch(this->hidden->emu_context, this->hidden->screenEvent);
			break;
		}
	}
#endif

#ifdef TOUCHPAD_SIMULATE
	if (state.pending[0] || state.pending[1]) {
		SDL_PrivateMouseMotion(state.mask, 1, state.pending[0], state.pending[1]);
		state.pending[0] = 0;
		state.pending[1] = 0;
	}
#endif
}

void PLAYBOOK_InitOSKeymap(_THIS)
{
//	discoverControllers(_priv->screenContext);

	{
		// We match perfectly from 32 to 64
		int i = 32;
		for (; i<65; i++)
		{
			Playbook_Keycodes[i].sym = i;
		}
		// Capital letters
		for (; i<91; i++)
		{
			Playbook_Keycodes[i].sym = i+32;
			Playbook_Keycodes[i].mod = KMOD_LSHIFT;
		}
		// Perfect matches again from 91 to 122
		for (; i<123; i++)
		{
			Playbook_Keycodes[i].sym = i;
		}

		// Handle tilde
		Playbook_Keycodes[126].sym = SDLK_PB_TILDE;
	}

	{
		Playbook_specialsyms = (SDLKey *)malloc(256 * sizeof(SDLKey));
		Playbook_specialsyms[SDLK_BACKSPACE] = SDLK_BACKSPACE;
		Playbook_specialsyms[SDLK_TAB] = SDLK_TAB;
		Playbook_specialsyms[SDLK_RETURN] = SDLK_RETURN;
		Playbook_specialsyms[SDLK_PAUSE] = SDLK_PAUSE;
		Playbook_specialsyms[SDLK_ESCAPE] = SDLK_ESCAPE;
		Playbook_specialsyms[0xff] = SDLK_DELETE;
		Playbook_specialsyms[0x52] = SDLK_UP;
		Playbook_specialsyms[0x54] = SDLK_DOWN;
		Playbook_specialsyms[0x53] = SDLK_RIGHT;
		Playbook_specialsyms[0x51] = SDLK_LEFT;
		Playbook_specialsyms[0x63] = SDLK_INSERT;
		Playbook_specialsyms[0x50] = SDLK_HOME;
		Playbook_specialsyms[0x57] = SDLK_END;
		Playbook_specialsyms[0x55] = SDLK_PAGEUP;
		Playbook_specialsyms[0x56] = SDLK_PAGEDOWN;
		Playbook_specialsyms[0xbe] = SDLK_F1;
		Playbook_specialsyms[0xbf] = SDLK_F2;
		Playbook_specialsyms[0xc0] = SDLK_F3;
		Playbook_specialsyms[0xc1] = SDLK_F4;
		Playbook_specialsyms[0xc2] = SDLK_F5;
		Playbook_specialsyms[0xc3] = SDLK_F6;
		Playbook_specialsyms[0xc4] = SDLK_F7;
		Playbook_specialsyms[0xc5] = SDLK_F8;
		Playbook_specialsyms[0xc6] = SDLK_F9;
		Playbook_specialsyms[0xc7] = SDLK_F10;
		Playbook_specialsyms[0xc8] = SDLK_F11;
		Playbook_specialsyms[0xc9] = SDLK_F12;
		Playbook_specialsyms[0xe5] = SDLK_CAPSLOCK;
		Playbook_specialsyms[0x14] = SDLK_SCROLLOCK;
		Playbook_specialsyms[0xe2] = SDLK_RSHIFT;
		Playbook_specialsyms[0xe1] = SDLK_LSHIFT;
		Playbook_specialsyms[0xe4] = SDLK_RCTRL;
		Playbook_specialsyms[0xe3] = SDLK_LCTRL;
		Playbook_specialsyms[0xe8] = SDLK_RALT;
		Playbook_specialsyms[0xe9] = SDLK_LALT;
		Playbook_specialsyms[0xbe] = SDLK_MENU;
		Playbook_specialsyms[0x61] = SDLK_SYSREQ;
		Playbook_specialsyms[0x6b] = SDLK_BREAK;
	}

#if 0 // Possible further keycodes that are available on the VKB
	Playbook_Keycodes[123].sym = SDLK_SPACE; /* { */
	Playbook_Keycodes[124].sym = SDLK_SPACE; /* | */
	Playbook_Keycodes[125].sym = SDLK_SPACE; /* } */
	Playbook_Keycodes[126].sym = SDLK_SPACE; /* ~ */
	Playbook_Keycodes[161].sym = SDLK_SPACE; /* upside down ! */
	Playbook_Keycodes[163].sym = SDLK_SPACE; /* British pound */
	Playbook_Keycodes[164].sym = SDLK_SPACE; /* odd circle/x */
	Playbook_Keycodes[165].sym = SDLK_SPACE; /* Japanese yen */
	Playbook_Keycodes[171].sym = SDLK_SPACE; /* small << */
	Playbook_Keycodes[177].sym = SDLK_SPACE; /* +/- */
	Playbook_Keycodes[183].sym = SDLK_SPACE; /* dot product */
	Playbook_Keycodes[187].sym = SDLK_SPACE; /* small >> */
	Playbook_Keycodes[191].sym = SDLK_SPACE; /* upside down ? */
	Playbook_Keycodes[247].sym = SDLK_SPACE; /* division */
	/*
	 * Playbook_Keycodes[8220] = smart double quote (top)
	 * Playbook_Keycodes[8221] = smart double quote (bottom)
	 * Playbook_Keycodes[8364] = euro
	 * Playbook_Keycodes[61448] = backspace
	 * Playbook_Keycodes[61453] = return
	 */
#endif
}

/* end of SDL_playbookevents.c ... */

