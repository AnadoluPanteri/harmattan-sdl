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

#include "SDL_x11video.h"
#include "../../events/SDL_events_c.h"
#include "SDL_x11gles_c.h"

#ifndef DEFAULT_EGL
#define DEFAULT_EGL "libEGL.so"
#endif

XVisualInfo *X11_GLES_GetVisual(_THIS)
{
#ifdef SDL_VIDEO_OPENGL_ES
	/* 64 seems nice. */
	EGLint attribs[64];
	EGLint found_configs = 0;
	int i;

	/* load the gl driver from a default path */
	if ( ! this->gl_config.driver_loaded ) {
	        /* no driver has been loaded, use default (ourselves) */
	        if ( X11_GLES_LoadLibrary(this, NULL) < 0 ) {
		        return NULL;
		}
	}

	/* TODO: Existing SDL_windowid; either get or change its visual. */

#if 0
    // Print out all of the available configs
    EGLConfig Configs[256];
    int Num;

    memset(Configs, 0, sizeof(Configs));

    this->gles_data->eglGetConfigs(egl_display, &Configs[0], sizeof(Configs) / sizeof(Configs[0]), &Num);

    printf("Found %d configs\n\n", Num);

    for (i = 0; i < Num; i++) {
        int Val;

        printf("\n[Config %d]\n", i);

        char Names[][32] = {
            "EGL_RED_SIZE",
            "EGL_GREEN_SIZE",
            "EGL_BLUE_SIZE",
            "EGL_ALPHA_SIZE",
            "EGL_BUFFER_SIZE",
            "EGL_DEPTH_SIZE",
            "EGL_STENCIL_SIZE",
            "EGL_SAMPLE_BUFFERS",
            "EGL_SAMPLES",
            "EGL_RENDERABLE_TYPE",
            "EGL_SURFACE_TYPE",
			"EGL_NATIVE_VISUAL_ID",
        };
        int ID[] = {
            EGL_RED_SIZE,
            EGL_GREEN_SIZE,
            EGL_BLUE_SIZE,
            EGL_ALPHA_SIZE,
            EGL_BUFFER_SIZE,
            EGL_DEPTH_SIZE,
            EGL_STENCIL_SIZE,
            EGL_SAMPLE_BUFFERS,
            EGL_SAMPLES,
            EGL_RENDERABLE_TYPE,
            EGL_SURFACE_TYPE,
			EGL_NATIVE_VISUAL_ID,
        };

        int j;
        for (j = 0; j < sizeof(ID) / sizeof(int); j++) {
            this->gles_data->eglGetConfigAttrib(egl_display, Configs[i], ID[j], &Val);

            printf("%s = 0x%x\n", Names[j], Val);
        }
    }

#endif
	i = 0;

    if (this->gl_config.red_size) {
    	attribs[i++] = EGL_RED_SIZE;
	    attribs[i++] = this->gl_config.red_size;
    }

    if (this->gl_config.green_size) {
    	attribs[i++] = EGL_GREEN_SIZE;
	    attribs[i++] = this->gl_config.green_size;
    }

    if (this->gl_config.blue_size) {
    	attribs[i++] = EGL_BLUE_SIZE;
	    attribs[i++] = this->gl_config.blue_size;
    }

	if( this->gl_config.alpha_size ) {
		attribs[i++] = EGL_ALPHA_SIZE;
		attribs[i++] = this->gl_config.alpha_size;
	}

	if( this->gl_config.buffer_size ) {
		attribs[i++] = EGL_BUFFER_SIZE;
		attribs[i++] = this->gl_config.buffer_size;
	}

    if (this->gl_config.depth_size) {
    	attribs[i++] = EGL_DEPTH_SIZE;
	    attribs[i++] = this->gl_config.depth_size;
    }

	if( this->gl_config.stencil_size ) {
		attribs[i++] = EGL_STENCIL_SIZE;
		attribs[i++] = this->gl_config.stencil_size;
	}

	if( this->gl_config.multisamplebuffers ) {
		attribs[i++] = EGL_SAMPLE_BUFFERS;
		attribs[i++] = this->gl_config.multisamplebuffers;
	}

	if( this->gl_config.multisamplesamples ) {
		attribs[i++] = EGL_SAMPLES;
		attribs[i++] = this->gl_config.multisamplesamples;
	}

    if(this->gl_config.major_version == 2) {
        attribs[i++] = EGL_RENDERABLE_TYPE;
        attribs[i++] = EGL_OPENGL_ES2_BIT;
    }

	int SurfaceTypeIndex = i;

	attribs[i++] = EGL_SURFACE_TYPE;
	attribs[i++] = EGL_WINDOW_BIT;

	attribs[i++] = EGL_NONE;

	if (this->gles_data->eglChooseConfig(egl_display, attribs,
		&egl_config, 1, &found_configs) == EGL_FALSE ||
		found_configs == 0) {

		SDL_SetError("Couldn't find a matching EGL configuration");
		return NULL;
	}

	EGLint value;
	VisualID visualid;

	this->gles_data->eglGetConfigAttrib(egl_display, egl_config, EGL_NATIVE_VISUAL_ID, &value);
	visualid = value;
	if (visualid) {
		XVisualInfo template = { .visualid = visualid };
        int count = 0;
        egl_visualinfo = XGetVisualInfo(GFX_Display, VisualIDMask, &template, &count);
		return egl_visualinfo;
	}

	SDL_SetError("Unable to find a matching X visual for EGL configuration");
	return NULL;
#else
	SDL_SetError("X11 driver not configured with OpenGL ES");
	return NULL;
#endif
}

int X11_GLES_CreateWindow(_THIS, int w, int h)
{
	int retval;

#if SDL_VIDEO_OPENGL_ES
	XSetWindowAttributes attributes;
	unsigned long mask;
	unsigned long black;

	black = (egl_visualinfo->visual == DefaultVisual(SDL_Display,
						 	SDL_Screen))
	       	? BlackPixel(SDL_Display, SDL_Screen) : 0;
	attributes.background_pixel = black;
	attributes.border_pixel = black;
	attributes.colormap = SDL_XColorMap;
	mask = CWBackPixel | CWBorderPixel | CWColormap;

	SDL_Window = XCreateWindow(SDL_Display, WMwindow,
			0, 0, w, h, 0, egl_visualinfo->depth,
			InputOutput, egl_visualinfo->visual,
			mask, &attributes);
	if ( !SDL_Window ) {
		SDL_SetError("Could not create window");
		return -1;
	}
	XFlush(SDL_Display);
	retval = 0;
#else
	SDL_SetError("X11 driver not configured with OpenGL ES");
	retval = -1;
#endif
	return(retval);
}

int X11_GLES_CreateContext(_THIS)
{
	int retval;

	/* Need to wait for window to be created first. */
	XSync( GFX_Display, False );

#if SDL_VIDEO_OPENGL_ES
	EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

	if (this->gl_config.major_version == 2)
		egl_context = this->gles_data->eglCreateContext(egl_display,
			egl_config, EGL_NO_CONTEXT, context_attribs);
    else
		egl_context = this->gles_data->eglCreateContext(egl_display,
			egl_config, EGL_NO_CONTEXT, NULL);

	if (egl_context == EGL_NO_CONTEXT) {
		SDL_SetError("Could not create EGL context");
		return -1;
	}

	egl_window = this->gles_data->eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)SDL_Window, NULL);

	if (egl_window == EGL_NO_SURFACE) {
		SDL_SetError("Could not create GLES window (error 0x%x\n)", this->gles_data->eglGetError());
		return -1;
	}

	if ( X11_GLES_MakeCurrent(this) < 0) {
		return -1;
	}

	egl_active = 1;
#else
	SDL_SetError("X11 driver not configured with OpenGL ES");
#endif

	if ( egl_active ) {
		retval = 0;
	} else {
		retval = -1;
	}
	return(retval);
}

void X11_GLES_Shutdown(_THIS)
{
#if SDL_VIDEO_OPENGL_ES
	/* Clean up OpenGL ES and EGL state */
	if (egl_active) {
		this->gles_data->eglMakeCurrent(egl_display, EGL_NO_SURFACE,
		                                EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (egl_context) {
			this->gles_data->eglDestroyContext(egl_display, egl_context);
			egl_context = EGL_NO_CONTEXT;
		}
		if (egl_window) {
			this->gles_data->eglDestroySurface(egl_display, egl_window);
			egl_window = EGL_NO_SURFACE;
		}
		egl_active = 0;
	}
#endif /* SDL_VIDEO_OPENGL_ES */
}

#if SDL_VIDEO_OPENGL_ES

/* Make the current context active */
int X11_GLES_MakeCurrent(_THIS)
{
	int retval;

	if ( !this->gles_data->eglMakeCurrent(egl_display, egl_window, egl_window, egl_context) ) {
		SDL_SetError("Unable to make EGL context current (error 0x%x)", this->gles_data->eglGetError());
		return -1;
	}

    if(this->gl_config.swap_control != -1) {
        this->gles_data->eglSwapInterval(egl_display, this->gl_config.swap_control);
    }

	return 0;
}

/* Get attribute data from EGL. */
int X11_GLES_GetAttribute(_THIS, SDL_GLattr attr, int* value)
{
	EGLint attrib = EGL_NONE;

	switch( attr ) {
	    case SDL_GL_RED_SIZE:
		attrib = EGL_RED_SIZE;
		break;
	    case SDL_GL_GREEN_SIZE:
		attrib = EGL_GREEN_SIZE;
		break;
	    case SDL_GL_BLUE_SIZE:
		attrib = EGL_BLUE_SIZE;
		break;
	    case SDL_GL_ALPHA_SIZE:
		attrib = EGL_ALPHA_SIZE;
		break;
	    case SDL_GL_BUFFER_SIZE:
		attrib = EGL_BUFFER_SIZE;
		break;
	    case SDL_GL_DEPTH_SIZE:
		attrib = EGL_DEPTH_SIZE;
		break;
	    case SDL_GL_STENCIL_SIZE:
		attrib = EGL_STENCIL_SIZE;
		break;
 	    case SDL_GL_MULTISAMPLEBUFFERS:
 		attrib = EGL_SAMPLE_BUFFERS;
 		break;
 	    case SDL_GL_MULTISAMPLESAMPLES:
 		attrib = EGL_SAMPLES;
 		break;
	    default:
		SDL_SetError("OpenGL ES attribute is unsupported on this system");
		return -1;
	}

	this->gles_data->eglGetConfigAttrib(egl_display, egl_config, attrib, value);

	return 0;
}

void X11_GLES_SwapBuffers(_THIS)
{
	this->gles_data->eglSwapBuffers(egl_display, egl_window);
}

#endif /* SDL_VIDEO_OPENGL_ES */

void X11_GLES_UnloadLibrary(_THIS)
{
#ifdef SDL_VIDEO_OPENGL_ES
	if ( this->gl_config.driver_loaded ) {
		this->gles_data->eglTerminate(egl_display);

		dlclose(this->gles_data->egl_handle);
        this->gles_data->egl_handle = NULL;

		this->gles_data->eglGetProcAddress = NULL;
		this->gles_data->eglGetError = NULL;
		this->gles_data->eglChooseConfig = NULL;
		this->gles_data->eglCreateContext = NULL;
		this->gles_data->eglCreateWindowSurface = NULL;
		this->gles_data->eglCreatePbufferSurface = NULL;
		this->gles_data->eglCreatePixmapSurface = NULL;
		this->gles_data->eglDestroyContext = NULL;
		this->gles_data->eglDestroySurface = NULL;
		this->gles_data->eglMakeCurrent = NULL;
		this->gles_data->eglSwapBuffers = NULL;
		this->gles_data->eglSwapInterval = NULL;
		this->gles_data->eglCopyBuffers = NULL;
		this->gles_data->eglGetDisplay = NULL;
		this->gles_data->eglTerminate = NULL;
		this->gles_data->eglSurfaceAttrib = NULL;
		this->gles_data->eglQuerySurface = NULL;

		this->gl_config.driver_loaded = 0;
	}
#endif
}

#if SDL_VIDEO_OPENGL_ES

#define OPENGL_REQUIRS_DLOPEN
#if defined(OPENGL_REQUIRS_DLOPEN) && defined(SDL_LOADSO_DLOPEN)
#include <dlfcn.h>
#define GL_LoadObject(X)	dlopen(X, (RTLD_NOW|RTLD_GLOBAL))
#define GL_LoadFunction		dlsym
#define GL_UnloadObject		dlclose
#else
#define GL_LoadObject	SDL_LoadObject
#define GL_LoadFunction	SDL_LoadFunction
#define GL_UnloadObject	SDL_UnloadObject
#endif

/*
 *  A macro for loading a function pointer with dlsym
 */
#define LOAD_FUNC(NAME) \
	this->gles_data->NAME = GL_LoadFunction(handle, #NAME); \
	if (!this->gles_data->NAME) \
	{ \
		SDL_SetError("Could not retrieve function " #NAME); \
		return -1; \
	}

/* Passing a NULL path means load pointers from the application */
int X11_GLES_LoadLibrary(_THIS, const char* path)
{
	void* handle = NULL;

	if ( egl_active ) {
		SDL_SetError("OpenGL ES context already created");
		return -1;
	}

	if ( path == NULL ) {
		path = SDL_getenv("SDL_VIDEO_EGL_DRIVER");
		if ( path == NULL ) {
			path = DEFAULT_EGL;
		}
	}

	handle = GL_LoadObject(path);
	if ( handle == NULL ) {
#if defined(OPENGL_REQUIRS_DLOPEN) && defined(SDL_LOADSO_DLOPEN)
		SDL_SetError("Failed loading %s", path);
#else
		/* SDL_LoadObject() will call SDL_SetError() for us. */
#endif
		return -1;
	}

	/* Unload the old driver and reset the pointers */
	X11_GLES_UnloadLibrary(this);

    /* Load EGL function pointers */
    LOAD_FUNC(eglGetError);
    LOAD_FUNC(eglGetDisplay);
    LOAD_FUNC(eglInitialize);
    LOAD_FUNC(eglTerminate);
    LOAD_FUNC(eglGetProcAddress);
    LOAD_FUNC(eglChooseConfig);
    LOAD_FUNC(eglGetConfigs);
    LOAD_FUNC(eglGetConfigAttrib);
    LOAD_FUNC(eglCreateContext);
    LOAD_FUNC(eglDestroyContext);
    LOAD_FUNC(eglCreateWindowSurface);
    LOAD_FUNC(eglCreatePbufferSurface);
    LOAD_FUNC(eglCreatePixmapSurface);
    LOAD_FUNC(eglDestroySurface);
    LOAD_FUNC(eglMakeCurrent);
    LOAD_FUNC(eglSwapBuffers);
    LOAD_FUNC(eglSwapInterval);
    LOAD_FUNC(eglCopyBuffers);
    LOAD_FUNC(eglSurfaceAttrib);
    LOAD_FUNC(eglQuerySurface);

	/* Initialize EGL */
	egl_display = this->gles_data->eglGetDisplay((EGLNativeDisplayType)GFX_Display);

	if (!egl_display) {
		SDL_SetError("Could not get EGL display");
		return -1;
	}

	if (this->gles_data->eglInitialize(egl_display, NULL, NULL) != EGL_TRUE) {
		SDL_SetError("Could not initialize EGL");
		return -1;
	}

	this->gles_data->egl_handle = handle;
	this->gl_config.driver_loaded = 1;
	if ( path ) {
		SDL_strlcpy(this->gl_config.driver_path, path,
			SDL_arraysize(this->gl_config.driver_path));
	} else {
		*this->gl_config.driver_path = '\0';
	}
	return 0;
}

void *X11_GLES_GetProcAddress(_THIS, const char* proc)
{
	void* handle;

	handle = this->gles_data->egl_handle;
	if ( this->gles_data->eglGetProcAddress ) {
		return this->gles_data->eglGetProcAddress(proc);
	}
	return GL_LoadFunction(handle, proc);
}

#endif /* SDL_VIDEO_OPENGL_ES */
