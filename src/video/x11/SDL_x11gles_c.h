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

#if SDL_VIDEO_OPENGL_ES
#include <EGL/egl.h>
#include "SDL_loadso.h"
#endif

#include "../SDL_sysvideo.h"

struct SDL_PrivateGLESData {
    int egl_active; /* to stop switching drivers while we have a valid context */

#if SDL_VIDEO_OPENGL_ES
	void *egl_handle;

	EGLDisplay egl_display;
	EGLContext egl_context;
	EGLSurface egl_window;
	EGLConfig  egl_config;

    XVisualInfo* egl_visualinfo; /* XVisualInfo* returned by eglChooseConfig */

	EGLint (*eglGetError)(void);

	EGLDisplay (*eglGetDisplay) (NativeDisplayType display);
	EGLBoolean (*eglInitialize) (EGLDisplay dpy, EGLint *major, EGLint *minor);
	EGLBoolean (*eglTerminate) (EGLDisplay dpy);

	void * (*eglGetProcAddress)(const GLubyte *procName);

	EGLBoolean (*eglChooseConfig) (EGLDisplay dpy,
                                   const EGLint *attrib_list,
                                   EGLConfig *configs,
                                   EGLint config_size,
                                   EGLint *num_config);

	EGLContext (*eglCreateContext) (EGLDisplay dpy,
	                                EGLConfig config,
	                                EGLContext share_list,
	                                const EGLint *attrib_list);

	EGLBoolean (*eglDestroyContext) (EGLDisplay dpy, EGLContext ctx);

	EGLSurface (*eglCreateWindowSurface) (EGLDisplay dpy,
	                                      EGLConfig config,
	                                      NativeWindowType window,
	                                      const EGLint *attrib_list);
	EGLSurface (*eglCreatePbufferSurface) (EGLDisplay dpy, EGLConfig config,
	                                       const EGLint *attrib_list);
	EGLSurface (*eglCreatePixmapSurface) (EGLDisplay dpy, EGLConfig config,
	                                      EGLNativePixmapType pixmap,
	                                      const EGLint *attrib_list);

	EGLBoolean (*eglDestroySurface) (EGLDisplay dpy, EGLSurface surface);

	EGLBoolean (*eglMakeCurrent) (EGLDisplay dpy, EGLSurface draw,
	                              EGLSurface read, EGLContext ctx);

	EGLBoolean (*eglSwapBuffers) (EGLDisplay dpy, EGLSurface draw);
	EGLBoolean (*eglSwapInterval) (EGLDisplay dpy, EGLint interval);
	EGLBoolean (*eglCopyBuffers) (EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target);

	const char *(*eglQueryString) (EGLDisplay dpy, EGLint name);

	EGLBoolean (*eglGetConfigs) (EGLDisplay dpy, EGLConfig *configs,
                                      EGLint config_size, EGLint *num_config);

	EGLBoolean (*eglGetConfigAttrib) (EGLDisplay dpy, EGLConfig config,
                                      EGLint attribute, EGLint *value);

	EGLBoolean (*eglSurfaceAttrib) (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value);
	EGLBoolean (*eglQuerySurface) (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);
#endif /* SDL_VIDEO_OPENGL_ES */
};

/* Direct variable names */
#define egl_active      (this->gles_data->egl_active)
#define egl_visualinfo  (this->gles_data->egl_visualinfo)
#define egl_display     (this->gles_data->egl_display)
#define egl_config      (this->gles_data->egl_config)
#define egl_context     (this->gles_data->egl_context)
#define egl_window      (this->gles_data->egl_window)

/* OpenGL ES functions */
XVisualInfo *X11_GLES_GetVisual(_THIS);
extern int X11_GLES_CreateWindow(_THIS, int w, int h);
extern int X11_GLES_CreateContext(_THIS);
extern void X11_GLES_Shutdown(_THIS);
#if SDL_VIDEO_OPENGL_ES
extern int X11_GLES_MakeCurrent(_THIS);
extern int X11_GLES_GetAttribute(_THIS, SDL_GLattr attrib, int* value);
extern void X11_GLES_SwapBuffers(_THIS);
extern int X11_GLES_LoadLibrary(_THIS, const char* path);
extern void *X11_GLES_GetProcAddress(_THIS, const char* proc);
#endif
extern void X11_GLES_UnloadLibrary(_THIS);

