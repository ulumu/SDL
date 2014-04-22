/*
 * SDL_playbookvideo_gl.c
 *
 *  Created on: 2012-01-17
 *      Author: asimonov
 */

#include "SDL_config.h"

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include <bps/bps.h>
#include <bps/screen.h>
#include <bps/event.h>
#include <bps/orientation.h>
#include <bps/navigator.h>

#include "touchcontroloverlay.h"

#include "SDL_playbookvideo_c.h"
#include "SDL_playbookvideo_gl_c.h"
#include "SDL_playbookevents_c.h"
#include "SDL_playbookhw_c.h"
#include "SDL_playbooktouch_c.h"
#include "SDL_playbookyuv_c.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <errno.h> // ::errno
#include <time.h> // struct tm, clock_gettime

static void egl_perror(const char *msg)
{
    static const char *errmsg[] = {
        "function succeeded",
        "EGL is not initialized, or could not be initialized, for the specified display",
        "cannot access a requested resource",
        "failed to allocate resources for the requested operation",
        "an unrecognized attribute or attribute value was passed in an attribute list",
        "an EGLConfig argument does not name a valid EGLConfig",
        "an EGLContext argument does not name a valid EGLContext",
        "the current surface of the calling thread is no longer valid",
        "an EGLDisplay argument does not name a valid EGLDisplay",
        "arguments are inconsistent",
        "an EGLNativePixmapType argument does not refer to a valid native pixmap",
        "an EGLNativeWindowType argument does not refer to a valid native window",
        "one or more argument values are invalid",
        "an EGLSurface argument does not name a valid surface configured for rendering",
        "a power management event has occurred",
    };

    fprintf(stderr, "%s: %s\n", msg, errmsg[eglGetError() - EGL_SUCCESS]);
}

static EGLint attributes_rgba8888[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 8,
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
#ifndef __X86__ // Simulator not supporting these options
	EGL_SAMPLE_BUFFERS, 1,
	EGL_SAMPLES, 4,
#endif
	EGL_NONE
};

static EGLint attributes_rgb565[] = {
	EGL_RED_SIZE,   5,
	EGL_GREEN_SIZE, 6,
	EGL_BLUE_SIZE,  5,
	EGL_ALPHA_SIZE, 0,
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
#ifndef __X86__ // Simulator not supporting these options
	EGL_SAMPLE_BUFFERS, 1,
	EGL_SAMPLES,    2,
#endif
	EGL_NONE
};

SDL_Surface *PLAYBOOK_SetVideoMode_GL(_THIS, SDL_Surface *current,
				int width, int height, int bpp, Uint32 flags)
{
	int rc;
	EGLint *attributes;
	EGLint contextAttributes[3] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLConfig configs[1];
	EGLint configCount;
	screen_window_t screenWindow;
	int format = SCREEN_FORMAT_RGB565;
	int usage = SCREEN_USAGE_OPENGL_ES2;
	EGLint eglSurfaceAttributes[3] = { EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE };
	int angle = 0;

	SLOG("GL2 Video setup, WIDTH:%d, HEIGHT:%d, bpp=%d", width, height, bpp);

	switch (bpp)
	{
	case 16:
		attributes = attributes_rgb565;
		format     = SCREEN_FORMAT_RGB565;
		break;

	case 32:
		attributes = attributes_rgba8888;
		format     = SCREEN_FORMAT_RGBX8888;
		break;

	default:
		SLOG("Unsupported bpp (%d)!!", bpp);
		goto error1;
	}

	if (!_priv->screenWindow) {

		SLOG("First time create Video EGL window");

		_priv->eglInfo.eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if (_priv->eglInfo.eglDisplay == EGL_NO_DISPLAY) {
			egl_perror("eglGetDisplay");
			goto error1;
		}

		rc = eglInitialize(_priv->eglInfo.eglDisplay, NULL, NULL);
		if (rc != EGL_TRUE) {
			egl_perror("eglInitialize");
			goto error1;
		}

		rc = eglBindAPI(EGL_OPENGL_ES_API);
		if (rc != EGL_TRUE) {
			egl_perror("eglBindAPI");
			goto error2;
		}

		rc = eglChooseConfig(_priv->eglInfo.eglDisplay, attributes, configs, 1, &configCount);
		if (rc != EGL_TRUE)	{
			egl_perror("eglBindAPI");
			eglTerminate(_priv->eglInfo.eglDisplay);
			return NULL;
		} else if (configCount <= 0)	{
			SLOG("No matching configurations found.");
//			goto error2;
		}

		_priv->eglInfo.eglContext = eglCreateContext(_priv->eglInfo.eglDisplay, configs[0], EGL_NO_CONTEXT, contextAttributes);
		if (_priv->eglInfo.eglContext == EGL_NO_CONTEXT)	{
			egl_perror("eglCreateContext");
			goto error2;
		}

		screenWindow = PLAYBOOK_CreateWindow(this, current, width, height, bpp);
		if (screenWindow == NULL) {
			goto error3;
		}

		rc = PLAYBOOK_SetupStretch(this, screenWindow, width, height, flags);
		if (rc) {
			goto error4;
		}

		rc = screen_set_window_property_iv(screenWindow, SCREEN_PROPERTY_FORMAT, &format);
		if (rc) {
			SDL_SetError("Cannot set window format: %s", strerror(errno));
			goto error4;
		}

		rc = screen_set_window_property_iv(screenWindow, SCREEN_PROPERTY_USAGE, &usage);
		if (rc) {
			SDL_SetError("Cannot set window usage: %s", strerror(errno));
			goto error4;
		}

		rc = screen_create_window_buffers(screenWindow, 2);
		if (rc) {
			SDL_SetError("Cannot create window buffers: %s", strerror(errno));
			goto error4;
		}

		_priv->eglInfo.eglSurface = eglCreateWindowSurface(_priv->eglInfo.eglDisplay, configs[0],
				screenWindow, (EGLint*)&eglSurfaceAttributes);
		if (_priv->eglInfo.eglSurface == EGL_NO_SURFACE) {
			egl_perror("eglCreateWindowSurface");
			goto error4;
		}

		rc = eglMakeCurrent(_priv->eglInfo.eglDisplay, _priv->eglInfo.eglSurface, _priv->eglInfo.eglSurface, _priv->eglInfo.eglContext);
		if (rc != EGL_TRUE) {
			egl_perror("eglMakeCurrent");
			goto error5;
		}
		locateTCOControlFile(this);
		if (_priv->tcoControlsDir) {
			initializeOverlay(this, screenWindow);
		}

		_priv->screenWindow = screenWindow;
	}
	else
	{
		tco_shutdown(_priv->emu_context);

		// Just resize for now
		rc = PLAYBOOK_SetupStretch(this, _priv->screenWindow, width, height, flags);
		if (rc) {
			goto error4;
		}

		if (_priv->tcoControlsDir) {
			initializeOverlay(this, _priv->screenWindow);
		}
	}

	current->flags &= ~SDL_RESIZABLE;
	current->flags |= SDL_FULLSCREEN;
	current->flags |= SDL_HWSURFACE;
	current->flags |= SDL_OPENGL;
	current->w      = width;
	current->h      = height;
	current->pitch  = width;
	current->pixels = 0;

	switch(bpp)
	{
	case 16:
		// RGB565 format setup
		current->format->BitsPerPixel  = 16;
		current->format->BytesPerPixel = 2;
		current->format->Bshift        = 0;
		current->format->Gshift        = 5;
		current->format->Rshift        = 11;
		current->format->Bmask         = 0x1F;
		current->format->Gmask         = 0x7E0;
		current->format->Rmask         = 0xF800;
		break;

	case 32:
		// RGB565 format setup
		current->format->BitsPerPixel  = 32;
		current->format->BytesPerPixel = 4;
		current->format->Bshift        = 0;
		current->format->Gshift        = 8;
		current->format->Rshift        = 16;
		current->format->Ashift        = 24;
		current->format->Bmask         = 0xFF;
		current->format->Gmask         = 0xFF00;
		current->format->Rmask         = 0xFF0000;
		current->format->Amask         = 0xFF000000;
		break;
	}


	_priv->surface = current;
	return current;

error5:
	eglDestroySurface(_priv->eglInfo.eglDisplay, _priv->eglInfo.eglSurface);
	_priv->eglInfo.eglSurface = 0;
error4:
	screen_destroy_window(screenWindow);
error3:
	eglDestroyContext(_priv->eglInfo.eglDisplay, _priv->eglInfo.eglContext);
	_priv->eglInfo.eglContext = 0;
error2:
	eglTerminate(_priv->eglInfo.eglDisplay);
	_priv->eglInfo.eglDisplay = 0;
error1:
	return NULL;
}

/* Sets the dll to use for OpenGL and loads it */
int PLAYBOOK_GL_LoadLibrary(_THIS, const char *path)
{
	fprintf(stderr, "%s\n", __FUNCTION__);
	return 0;
}

/* Retrieves the address of a function in the gl library */
void* PLAYBOOK_GL_GetProcAddress(_THIS, const char *proc) {
	fprintf(stderr, "%s\n", __FUNCTION__);
	return 0;
}

/* Get attribute information from the windowing system. */
int PLAYBOOK_GL_GetAttribute(_THIS, SDL_GLattr attrib, int* value)
{
	fprintf(stderr, "%s\n", __FUNCTION__);
	return 0;
}

/* Make the context associated with this driver current */
int PLAYBOOK_GL_MakeCurrent(_THIS)
{
	int rc = eglMakeCurrent(_priv->eglInfo.eglDisplay, _priv->eglInfo.eglSurface, _priv->eglInfo.eglSurface, _priv->eglInfo.eglContext);
//	fprintf(stderr, "%s: %d\n", __FUNCTION__, rc);
	return rc;
}

/* Swap the current buffers in double buffer mode. */
void PLAYBOOK_GL_SwapBuffers(_THIS)
{
//	fprintf(stderr, "%s\n", __FUNCTION__);
	eglSwapBuffers(_priv->eglInfo.eglDisplay, _priv->eglInfo.eglSurface);
}
