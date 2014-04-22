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
#include "SDL_config.h"
#include "touchcontroloverlay.h"

#if defined(SDL_JOYSTICK_PLAYBOOK)

/* This is the system specific header for the SDL joystick API */

#include "SDL_events.h"
#include "SDL_joystick.h"
#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"
#include "../../video/SDL_sysvideo.h"
#include "../../video/playbook/SDL_playbookvideo.h"

#include <bps/screen.h>
#include <bps/event.h>
#include <stdio.h>
#include <errno.h>

int rc;

#define SCREEN_API(x, y) rc = x; \
    if (rc) SLOG("%s in %s: %d", y, __FUNCTION__, errno)

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

    int  dpadPressState[4];
    int  buttonPressState[24];
} GameController_t;

// Controller information.
#define MAX_CONTROLLERS  2
static GameController_t gGameController[MAX_CONTROLLERS];

#define KEY_A     (0)
#define KEY_B     (1)
#define KEY_C     (2)
#define KEY_X     (3)
#define KEY_Y     (4)
#define KEY_Z     (5)
#define KEY_MENU1 (6)
#define KEY_MENU2 (7)
#define KEY_MENU3 (8)
#define KEY_MENU4 (9)
#define KEY_L1    (10)
#define KEY_L2    (11)
#define KEY_L3    (12)
#define KEY_R1    (13)
#define KEY_R2    (14)
#define KEY_R3    (15)
#define KEY_UP    (16)
#define KEY_DOWN  (17)
#define KEY_LEFT  (18)
#define KEY_RIGHT (19)


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

static void loadControllerInfo(GameController_t* controller)
{
    // Query libscreen for information about this device.
    SCREEN_API(screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_TYPE, &controller->type), "SCREEN_PROPERTY_TYPE");
    SCREEN_API(screen_get_device_property_cv(controller->handle, SCREEN_PROPERTY_ID_STRING, sizeof(controller->id), controller->id), "SCREEN_PROPERTY_ID_STRING");
    SCREEN_API(screen_get_device_property_cv(controller->handle, SCREEN_PROPERTY_VENDOR, sizeof(controller->vendor), controller->vendor), "SCREEN_PROPERTY_VENDOR");
    SCREEN_API(screen_get_device_property_cv(controller->handle, SCREEN_PROPERTY_PRODUCT, sizeof(controller->product), controller->product), "SCREEN_PROPERTY_PRODUCT");
    SCREEN_API(screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_BUTTON_COUNT, &controller->buttonCount), "SCREEN_PROPERTY_BUTTON_COUNT");

    // Check for the existence of analog sticks.
    if (!screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_ANALOG0, controller->analog0)) {
    	++controller->analogCount;
    }

    if (!screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_ANALOG1, controller->analog0)) {
    	++controller->analogCount;
    }

    sprintf(controller->deviceString, "%s-%s-%s", controller->vendor, controller->product, controller->id);
}

static int discoverControllers(screen_context_t screenCtx)
{
    // Get an array of all available devices.
    int              deviceCount = 0;
    int              i;
    int              controllerIndex = 0;
    screen_device_t* devices = NULL;

    SCREEN_API(screen_get_context_property_iv(screenCtx, SCREEN_PROPERTY_DEVICE_COUNT, &deviceCount), "SCREEN_PROPERTY_DEVICE_COUNT");

    SLOG("Device discovery, %d found", deviceCount);

    devices = (screen_device_t*)calloc(deviceCount, sizeof(screen_device_t));
    if (NULL == devices)
    {
    	SDL_OutOfMemory();
    	return 0;
    }

    SCREEN_API(screen_get_context_property_pv(screenCtx, SCREEN_PROPERTY_DEVICES, (void**)devices), "SCREEN_PROPERTY_DEVICES");

    // Scan the list for gamepad and joystick devices.
    for (i = 0; i < deviceCount; i++) {
        int type;
        SCREEN_API(screen_get_device_property_iv(devices[i], SCREEN_PROPERTY_TYPE, &type), "SCREEN_PROPERTY_TYPE");

        if (!rc && (type == SCREEN_EVENT_GAMEPAD || type == SCREEN_EVENT_JOYSTICK)) {
        	SLOG("Joystick %d Found", i);

            // Assign this device to control Player 1 or Player 2.
        	GameController_t* controller = &gGameController[controllerIndex];
            controller->handle = devices[i];
            loadControllerInfo(controller);

            SLOG("Joystick ID:%s", controller->deviceString);

            // We'll just use the first compatible devices we find.
            controllerIndex++;
            if (controllerIndex == MAX_CONTROLLERS) {
                break;
            }
        }
    }

    free(devices);

    return controllerIndex;
}

/* Function to scan the system for joysticks.
 * This function should set SDL_numjoysticks to the number of available
 * joysticks.  Joystick 0 should be the system default joystick.
 * It should return 0, or -1 on an unrecoverable fatal error.
 */
int SDL_SYS_JoystickInit(void)
{
	SDL_VideoDevice *this = current_video;

	if (this)
	{
		memset(gGameController, 0, sizeof(gGameController));

		SDL_numjoysticks = discoverControllers(this->hidden->screenContext);
	}
	else
	{
		SLOG("No current video is initialized, set Joystick count to 0");
		SDL_numjoysticks = 0;
	}

	if (SDL_numjoysticks > 0)
	{
		SLOG("Found %d joystick, disabling on-screen overlay icon", SDL_numjoysticks);

		_priv->hideTco = 1;
		if (_priv->tcoControlsDir)
			tco_hidelabels(_priv->emu_context, _priv->screenWindow);

		return (SDL_numjoysticks);
	}
	else
	{
		SLOG("No joystick found, set hideTco to false");
		_priv->hideTco = 0;
		return(-1);
	}
}

/* Function to get the device-dependent name of a joystick */
const char *SDL_SYS_JoystickName(int index)
{
	if (index >= MAX_CONTROLLERS)
	{
		SDL_SetError("Logic error: joysticks index out of range");
		return(NULL);
	}

	return gGameController[index].deviceString;
}

/* Function to open a joystick for use.
   The joystick to open is specified by the index field of the joystick.
   This should fill the nbuttons and naxes fields of the joystick structure.
   It returns 0, or -1 if there is an error.
 */
int SDL_SYS_JoystickOpen(SDL_Joystick *joystick)
{
	if (joystick && (joystick->index < MAX_CONTROLLERS) )
	{
		joystick->nbuttons = gGameController[joystick->index].buttonCount;
		joystick->nhats    = 0;
		joystick->nballs   = 0;
		joystick->naxes    = gGameController[joystick->index].analogCount;
	}
	else
	{
		SDL_SetError("Logic error: No joysticks available");
		return(-1);
	}

	return (0);
}

/* Function to update the state of a joystick - called as a device poll.
 * This function shouldn't update the joystick structure directly,
 * but instead should call SDL_PrivateJoystick*() to deliver events
 * and update joystick device state.
 */
void SDL_SYS_JoystickUpdate(SDL_Joystick *joystick)
{
	GameController_t *controller;
	int               sdlState = SDL_PRESSED;
	int               i;
	int               tmp[32];

	if (joystick == NULL)
		return;

	controller = &gGameController[joystick->index];

	screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_BUTTONS, &controller->buttons);

    if (controller->analogCount > 0) {
    	SCREEN_API(screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_ANALOG0, controller->analog0), "SCREEN_PROPERTY_ANALOG0");
    }

    if (controller->analogCount == 2) {
        SCREEN_API(screen_get_device_property_iv(controller->handle, SCREEN_PROPERTY_ANALOG1, controller->analog1), "SCREEN_PROPERTY_ANALOG1");
    }

    memset(tmp, 0, sizeof(tmp));
    if (controller->buttons & GAME_DPAD_MASK)
    {
    	if (controller->buttons & SCREEN_DPAD_UP_GAME_BUTTON)
    	{
    		tmp[0] = KEY_UP;
    	}
    	if (controller->buttons & SCREEN_DPAD_DOWN_GAME_BUTTON)
    	{
    		tmp[1] = KEY_DOWN;
    	}
    	if (controller->buttons & SCREEN_DPAD_RIGHT_GAME_BUTTON)
    	{
    		tmp[2] = KEY_RIGHT;
    	}
    	if (controller->buttons & SCREEN_DPAD_LEFT_GAME_BUTTON)
    	{
    		tmp[3] = KEY_LEFT;
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
				controller->dpadPressState[i] = tmp[i];
			}
			else
			{
				sdlState = SDL_RELEASED;
				tmp[i] = controller->dpadPressState[i];
				controller->dpadPressState[i] = 0;

			}
			SDL_PrivateJoystickButton(joystick, tmp[i], sdlState);
		}
	}


	memset(tmp, 0, sizeof(tmp));
    if (controller->buttons & (GAME_BUTTON_MASK | GAME_MENU_MASK) )
    {
    	if (controller->buttons & SCREEN_A_GAME_BUTTON)
    	{
    		tmp[0] = KEY_A;
    	}
    	if (controller->buttons & SCREEN_B_GAME_BUTTON)
    	{
    		tmp[1] = KEY_B;
    	}
    	if (controller->buttons & SCREEN_C_GAME_BUTTON)
    	{
    		tmp[2] = KEY_C;
    	}
    	if (controller->buttons & SCREEN_X_GAME_BUTTON)
    	{
    		tmp[3] = KEY_X;
    	}
    	if (controller->buttons & SCREEN_Y_GAME_BUTTON)
    	{
    		tmp[4] = KEY_Y;
    	}
    	if (controller->buttons & SCREEN_Z_GAME_BUTTON)
    	{
    		tmp[5] = KEY_Z;
    	}
    	if (controller->buttons & SCREEN_L1_GAME_BUTTON )
    	{
    		tmp[6] = KEY_L1;
    	}
    	if (controller->buttons & SCREEN_L2_GAME_BUTTON )
    	{
    		tmp[7] = KEY_L2;
    	}
    	if (controller->buttons & SCREEN_L3_GAME_BUTTON )
    	{
    		tmp[8] = KEY_L3;
    	}
    	if (controller->buttons & SCREEN_R1_GAME_BUTTON )
    	{
    		tmp[9] = KEY_R1;
    	}
    	if (controller->buttons & SCREEN_R2_GAME_BUTTON )
    	{
    		tmp[10] = KEY_R2;
    	}
    	if (controller->buttons & SCREEN_R3_GAME_BUTTON )
    	{
    		tmp[11] = KEY_R3;
    	}
    	if (controller->buttons & SCREEN_MENU1_GAME_BUTTON)
    	{
    		tmp[12] = KEY_MENU1;
    	}
    	if (controller->buttons & SCREEN_MENU2_GAME_BUTTON )
    	{
    		tmp[13] = KEY_MENU2 ;
    	}
    	if (controller->buttons & SCREEN_MENU3_GAME_BUTTON )
    	{
    		tmp[14] = KEY_MENU3 ;
    	}
    	if (controller->buttons & SCREEN_MENU4_GAME_BUTTON )
    	{
    		tmp[15] = KEY_MENU4 ;
    	}
    }

	for (i=0; i<16; i++)
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

			SDL_PrivateJoystickButton(joystick, tmp[i], sdlState);
		}
	}

	return;
}

/* Function to close a joystick after use */
void SDL_SYS_JoystickClose(SDL_Joystick *joystick)
{
	return;
}

/* Function to perform any system-specific joystick related cleanup */
void SDL_SYS_JoystickQuit(void)
{
	return;
}

#endif /* SDL_JOYSTICK_DUMMY || SDL_JOYSTICK_DISABLED */
