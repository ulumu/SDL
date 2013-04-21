/*
 * SDL_playbooktouch.c
 *
 *  Created on: Nov 23, 2011
 *      Author: jnicholl
 */

#include "SDL_config.h"

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "touchcontroloverlay.h"
#include "SDL_playbookvideo.h"
#include <unistd.h>

int handleKey(int sym, int mod, int scancode, uint16_t unicode, int event)
{
	int sdlEvent;
	switch (event)
	{
	case TCO_KB_DOWN:
		sdlEvent = SDL_PRESSED;
		break;
	case TCO_KB_UP:
		sdlEvent = SDL_RELEASED;
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
	return TCO_SUCCESS;
}

int handleDPad(int angle, int event)
{
	static int pressed[4] = {0, 0, 0, 0}; // Up, Down, Right, Left
	int tmp[4] = {0,0,0,0};
	switch (event)
	{
	case TCO_KB_DOWN:
		{
			if (angle <= -158 || angle >= 158) {
				// Left: -180 to -158, 158 to 180
				tmp[3] = 1;
			} else if (angle <= -103) {
				tmp[3] = 1;
				tmp[0] = 1;
				// Up-Left: -157 to -103
			} else if (angle <= -68) {
				tmp[0] = 1;
				// Up: -68 to -102
			} else if (angle <= -23) {
				tmp[0] = 1;
				tmp[2] = 1;
				// Up-Right: -23 to -67
			} else if (angle <= 22) {
				tmp[2] = 1;
				// Right: -22 to 22
			} else if (angle <= 67) {
				tmp[1] = 1;
				tmp[2] = 1;
				// Down-Right: 23 to 67
			} else if (angle <= 102) {
				tmp[1] = 1;
				// Down: 68 to 102
			} else if (angle <= 157) {
				tmp[1] = 1;
				tmp[3] = 1;
				// Down-Left: 103 to 157
			} else {
				fprintf(stderr, "Unknown dpad angle: %d\n", angle);
				return TCO_UNHANDLED;
			}
		}
		break;
	case TCO_KB_UP:
		break;
	default:
		return TCO_UNHANDLED;
	}

	int sdlState = SDL_PRESSED;
	SDL_keysym keysym;
	int scancodes[4] = {72, 75, 77, 80}; // From DosBox, keyboard.cpp
	int symcodes[4] = {SDLK_w, SDLK_s, SDLK_d, SDLK_a};
	int i;
	for (i=0; i<4; i++) {
		if (pressed[i] != tmp[i]) {
			if (tmp[i]) {
				sdlState = SDL_PRESSED;
			} else {
				sdlState = SDL_RELEASED;
			}
			keysym.sym = symcodes[i];
			keysym.scancode = scancodes[i];
			SDL_PrivateKeyboard(sdlState, &keysym);
			pressed[i] = tmp[i];
		}
	}
	return TCO_SUCCESS;
}

int handleTouch(int dx, int dy)
{
	SDL_PrivateMouseMotion(SDL_GetMouseState(0, 0), 1, dx, dy);
	return TCO_SUCCESS;
}

int handleMouseButton(int button, int mask, int event)
{
	int mouseX, mouseY;
	int sdlEvent;
	int sdlButton;

	switch (event)
	{
	case TCO_MOUSE_BUTTON_UP:
		sdlEvent = SDL_RELEASED;
		break;
	case TCO_MOUSE_BUTTON_DOWN:
		sdlEvent = SDL_PRESSED;
		break;
	default:
		fprintf(stderr, "No mouse button event?? (%d)\n", event);
		sdlEvent = SDL_PRESSED;
		break;
	}

	switch (button)
	{
	case TCO_MOUSE_LEFT_BUTTON:
		sdlButton = SDL_BUTTON_LEFT;
		break;
	case TCO_MOUSE_RIGHT_BUTTON:
		sdlButton = SDL_BUTTON_RIGHT;
		break;
	case TCO_MOUSE_MIDDLE_BUTTON:
		sdlButton = SDL_BUTTON_MIDDLE;
		break;
	default:
		fprintf(stderr, "No mouse button?? (%d)\n", button);
		sdlButton = SDL_BUTTON_LEFT;
		break;
	}
	SDL_GetMouseState(&mouseX, &mouseY);
	mouseY += (current_video->hidden)->eventYOffset;

	SDL_keysym shift, ctrl, alt;
	shift.scancode = 42;
	shift.sym = SDLK_LSHIFT;
	ctrl.scancode = 29;
	ctrl.sym = SDLK_LCTRL;
	alt.scancode = 56;
	alt.sym = SDLK_LALT;

	if (sdlEvent == SDL_PRESSED) {
		if (mask & TCO_SHIFT) {
			SDL_PrivateKeyboard(SDL_PRESSED, &shift);
		}
		if (mask & TCO_CTRL) {
			SDL_PrivateKeyboard(SDL_PRESSED, &ctrl);
		}
		if (mask & TCO_ALT) {
			SDL_PrivateKeyboard(SDL_PRESSED, &alt);
		}
	}
	SDL_PrivateMouseButton(sdlEvent, sdlButton, mouseX, mouseY);
	if (sdlEvent == SDL_RELEASED) {
		if (mask & TCO_SHIFT) {
			SDL_PrivateKeyboard(SDL_RELEASED, &shift);
		}
		if (mask & TCO_CTRL) {
			SDL_PrivateKeyboard(SDL_RELEASED, &ctrl);
		}
		if (mask & TCO_ALT) {
			SDL_PrivateKeyboard(SDL_RELEASED, &alt);
		}
	}
	return TCO_SUCCESS;
}

int handleTap()
{
	int mouseX, mouseY;
	SDL_GetMouseState(&mouseX, &mouseY);
	SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_LEFT, mouseX, mouseY);
	SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_LEFT, mouseX, mouseY);
	return TCO_SUCCESS;
}

int handleTouchScreen(int x, int y, int tap, int hold)
{
	if (tap) {
		SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_LEFT, x, y);
		SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_LEFT, x, y);
	} else if (hold) {
		SDL_PrivateMouseButton(SDL_PRESSED, SDL_BUTTON_RIGHT, x, y);
		SDL_PrivateMouseButton(SDL_RELEASED, SDL_BUTTON_RIGHT, x, y);
	} else {
		SDL_PrivateMouseMotion(SDL_GetMouseState(0, 0), 0, x, y);
	}
	return TCO_SUCCESS;
}

const char *control_device_useoverlay  = "sdl-controls.xml";
const char *control_device_usekeyboard = "sdl-controls-keyboard.xml";

void locateTCOControlFile(_THIS)
{
    const char *filename = control_device_useoverlay;
    char *homeDir = SDL_getenv("HOME");
    char  fullPath[512];
    FILE *fd = NULL;

    // Use SDL multi-mouse controls as default
    _priv->tcoControlsDir = 0;

	// HACK: Handle Q10 device, hide overlay button if Q10 device is detected
    SLOG("Detected screen resolution: %d x %d", _priv->SDL_modelist[0]->w, _priv->SDL_modelist[0]->h);
	if (_priv->SDL_modelist[0]->w == _priv->SDL_modelist[0]->h)
	{
		SLOG("Device with keyboard detected, hiding Overlay buttons");

		filename = control_device_usekeyboard;
	}

    if (homeDir == NULL)
    {
    	return;
    }

    sprintf(fullPath, "%s/%s", homeDir, filename);
    fd = fopen(fullPath, "r");

    if (fd == NULL)
    {
    	sprintf(fullPath, "%s/../%s", homeDir, filename);
		fd = fopen(fullPath, "r");
    }

    if (fd == NULL)
    {
        sprintf(fullPath, "%s/../app/native/%s", homeDir, filename);
        fd = fopen(fullPath, "r");
    }

    if (fd)
    {
        _priv->tcoControlsDir  = SDL_malloc(strlen(fullPath) - strlen(filename) + 1);
        _priv->tcoControlsFile = SDL_malloc(strlen(filename) + 1);
        if (_priv->tcoControlsDir)
        {
			strncpy(_priv->tcoControlsDir, fullPath, strlen(fullPath) - strlen(filename));
			_priv->tcoControlsDir[strlen(fullPath)-strlen(filename)] = '\0';
        }
        if (_priv->tcoControlsFile)
        {
			strncpy(_priv->tcoControlsFile, filename, strlen(filename));
			_priv->tcoControlsFile[strlen(filename)] = '\0';
        }
        fclose(fd);
    }

}

void initializeOverlay(_THIS, screen_window_t screenWindow)
{
	int loaded = 0;
	const char *filename = _priv->tcoControlsFile;
	struct tco_callbacks callbacks = {
		handleKey, handleDPad, handleTouch, handleMouseButton, handleTap, handleTouchScreen
	};

	if(!_priv->tcoControlsDir) {
		// Immediately fall back to SDL multi-mouse controls
		SLOG("Unable to initialize TCO with a NULL tcoControlsDir");
		return;
	}

	tco_initialize(&_priv->emu_context, _priv->screenContext, callbacks);

	// Load controls from file
	char cwd[256];
	if ((getcwd(cwd, 256) != NULL) && (chdir(_priv->tcoControlsDir) == 0)) {
		if (tco_loadcontrols(_priv->emu_context, filename) == TCO_SUCCESS) {
			loaded = 1;
		}
		chdir(cwd);
	}

	// Clean up and set flags
	SDL_free(_priv->tcoControlsDir);
	SDL_free(_priv->tcoControlsFile);
	_priv->tcoControlsFile = NULL;
	if (loaded) {
		_priv->tcoControlsDir = 1;

		// hideTco is set within SDL_SYS_JoystickInit
		// if a joystick is detected, then there is no need to display the overlay Label
		if (_priv->hideTco == 0)
			tco_showlabels(_priv->emu_context, screenWindow);
		else
			tco_hidelabels(_priv->emu_context, screenWindow);
	} else {
		tco_shutdown(&_priv->emu_context);
		_priv->tcoControlsDir = 0;
	}
}
