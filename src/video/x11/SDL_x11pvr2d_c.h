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
	Javier S. Pedro
	maemo@javispedro.com

*/

#ifndef _SDL_x11pvr2d_c_h
#define _SDL_x11pvr2d_c_h

#include "SDL_config.h"
#include "SDL_x11video.h"
#include <pvr2d.h>

/* Surfaces smaller than this many pixels should be software blitted. */
#define PVR2D_THRESHOLD_SIZE 32768

extern PVR2DCONTEXTHANDLE pvr2d_ctx;

extern int X11_PVR2D_AddRef(_THIS);
extern int X11_PVR2D_DecRef(_THIS);
extern void X11_PVR2D_WaitFlip(_THIS);

extern int X11_PVR2D_SetupImage(_THIS, SDL_Surface *screen);
extern void X11_PVR2D_DestroyImage(_THIS, SDL_Surface *screen);
extern void * X11_PVR2D_MapBuffer(_THIS, DRI2Buffer *buffer, void **buf);
extern void X11_PVR2D_UnmapBuffer(_THIS, void *buf);

extern void X11_PVR2D_ClearCache(_THIS, SDL_Surface *surface);
extern void X11_PVR2D_WaitBlits(_THIS, SDL_Surface *surface);

extern int X11_PVR2D_AllocSurface(_THIS, SDL_Surface *surface);
extern void X11_PVR2D_FreeSurface(_THIS, SDL_Surface *surface);

extern int X11_PVR2D_LockHWSurface(_THIS, SDL_Surface *surface);
extern void X11_PVR2D_UnlockHWSurface(_THIS, SDL_Surface *surface);

extern int X11_PVR2D_FillHWRect(_THIS, SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);

extern int X11_PVR2D_HWBlit(SDL_Surface *src, SDL_Rect *srcrect,
					SDL_Surface *dst, SDL_Rect *dstrect);

#endif /* _SDL_x11pvr2d_c_h */

