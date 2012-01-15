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

#include <stdio.h>
#include <unistd.h>

#include "SDL_endian.h"
#include "../../events/SDL_events_c.h"
#include "../SDL_pixels_c.h"
#include "SDL_x11image_c.h"

#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
#include <sys/shm.h>
#include "SDL_x11pvr2d_c.h"
#endif

#ifndef NO_SHARED_MEMORY

/* Shared memory error handler routine */
static int shm_error;
static int (*X_handler)(Display *, XErrorEvent *) = NULL;
static int shm_errhandler(Display *d, XErrorEvent *e)
{
        if ( e->error_code == BadAccess ) {
        	shm_error = True;
        	return(0);
        } else
		return(X_handler(d,e));
}

static void try_mitshm(_THIS, SDL_Surface *screen)
{
	/* Dynamic X11 may not have SHM entry points on this box. */
	if ((use_mitshm) && (!SDL_X11_HAVE_SHM))
		use_mitshm = 0;

	if(!use_mitshm)
		return;
	shminfo.shmid = shmget(IPC_PRIVATE, screen->h*screen->pitch,
			       IPC_CREAT | 0777);
	if ( shminfo.shmid >= 0 ) {
		shminfo.shmaddr = (char *)shmat(shminfo.shmid, 0, 0);
		shminfo.readOnly = False;
		if ( shminfo.shmaddr != (char *)-1 ) {
			shm_error = False;
			X_handler = XSetErrorHandler(shm_errhandler);
			XShmAttach(SDL_Display, &shminfo);
			XSync(SDL_Display, True);
			XSetErrorHandler(X_handler);
			if ( shm_error )
				shmdt(shminfo.shmaddr);
		} else {
			shm_error = True;
		}
		shmctl(shminfo.shmid, IPC_RMID, NULL);
	} else {
		shm_error = True;
	}
	if ( shm_error )
		use_mitshm = 0;
	if ( use_mitshm )
		screen->pixels = shminfo.shmaddr;
}
#endif /* ! NO_SHARED_MEMORY */

#if SDL_VIDEO_DRIVER_X11_DRI2
/* Keep this to a single one */
static unsigned int dri2_attachments[1] = {
	DRI2BufferBackLeft
};

static void X11_DRI2_RemoveBuffer(_THIS, int i)
{
	if (dri2_cache[i].valid) {
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
		if (dri2_cache[i].buf.flags & 1) {
			X11_PVR2D_UnmapBuffer(this, dri2_cache[i].dev_mem);
		} else {
			shmdt(dri2_cache[i].mem);
		}
#endif
		dri2_cache[i].mem = NULL;
		dri2_cache[i].valid = 0;
	}
}

static int X11_DRI2_FindEmpty(_THIS)
{
	int i;
	for (i = 0; i < DRI2_BUFFER_CACHE_SIZE; i++) {
		if (!dri2_cache[i].valid) {
			return i;
		}
	}
	/* If we are here, cache was full. Eject oldest line from it. */
	X11_DRI2_RemoveBuffer(this, 0);
	memmove(&dri2_cache[0], &dri2_cache[1], DRI2_BUFFER_CACHE_SIZE * sizeof(dri2_cache[0]));
	dri2_cache[DRI2_BUFFER_CACHE_SIZE - 1].valid = 0;
	return DRI2_BUFFER_CACHE_SIZE - 1;
}

static int X11_DRI2_FindInCache(_THIS, const DRI2Buffer *buffer)
{
	int i;
	for (i = 0; i < DRI2_BUFFER_CACHE_SIZE; i++) {
		if (dri2_cache[i].valid &&
		      memcmp(&dri2_cache[i].buf, buffer, sizeof(*buffer)) == 0) {
			return i;
		}
	}
	return -1;
}

static int X11_DRI2_CacheBuffer(_THIS, const DRI2Buffer *buffer)
{
	int i = X11_DRI2_FindInCache(this, buffer);
	if (i != -1) {
		/* Found in cache, so this buffer is already mmaped. */
		return i;
	}

	i = X11_DRI2_FindEmpty(this);
	dri2_cache[i].buf = *buffer;
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	if (dri2_cache[i].buf.flags & 1) {
		dri2_cache[i].mem = X11_PVR2D_MapBuffer(this, &dri2_cache[i].buf, &dri2_cache[i].dev_mem);
	} else {
		dri2_cache[i].mem = shmat(dri2_cache[i].buf.name, NULL, 0);
		if (dri2_cache[i].mem == (void*) -1) {
			dri2_cache[i].mem = NULL;
		}
	}
#endif
	dri2_cache[i].valid = dri2_cache[i].mem != NULL;
	if (!dri2_cache[i].valid) {
		return -1;
	}
	return i;
}

static void X11_DRI2_InvalidateCache(_THIS)
{
	int i;
	for (i = 0; i < DRI2_BUFFER_CACHE_SIZE; i++) {
		X11_DRI2_RemoveBuffer(this, i);
	}
}

static int X11_DRI2_PrepareVideoSurface(_THIS)
{
	if (dri2_buf == -1 || !dri2_cache[dri2_buf].valid) {
		/* Bad. Our current mapping was invalidated. Roundtrip to get a new one. */
		int w, h, in_count = 1, out_count = 0;
		DRI2Buffer *buffers = DRI2GetBuffers(SDL_Display, SDL_Window, &w, &h,
			dri2_attachments, in_count, &out_count);
		if (out_count < 1 || buffers == NULL) return -1;

		dri2_buf = X11_DRI2_CacheBuffer(this, &buffers[0]);
		if (dri2_buf == -1) {
			return -1;
		}

#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
		int new_accel = dri2_cache[dri2_buf].buf.flags & 1;
		if (new_accel != dri2_accel) {
			/* Bad. Fullscreen status just changed.
			   Maybe we can't use PVR2D any longer! */
			SDL_FormatChanged(SDL_VideoSurface);
			dri2_accel = new_accel;
		}
		if (dri2_accel) {
			X11_PVR2D_SetupImage(this, SDL_VideoSurface);
		}
#endif
	}

	return 0;
}

static int X11_DRI2_LockVideoSurface(_THIS, SDL_Surface *surface)
{
	int r = X11_DRI2_PrepareVideoSurface(this);
	if (r != 0) return r;

#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	if (dri2_accel) {
		X11_PVR2D_WaitBlits(this, surface);
	}
#endif

	surface->pixels = dri2_cache[dri2_buf].mem;
	surface->pitch = dri2_cache[dri2_buf].buf.pitch;

	return 0;
}

static void X11_DRI2_UnlockVideoSurface(_THIS, SDL_Surface *surface)
{
	surface->pixels = NULL;
}

static void X11_DRI2_Update(_THIS, int numrects, SDL_Rect *rects)
{
	XRectangle *xrects = calloc(numrects, sizeof(XRectangle));
	XserverRegion region;
	int i;

	for ( i=0; i<numrects; ++i ) {
		xrects[i].x = rects[i].x;
		xrects[i].y = rects[i].y;
		xrects[i].width = rects[i].w;
		xrects[i].height = rects[i].h;
	}

	region = XFixesCreateRegion(SDL_Display, xrects, numrects);
	DRI2CopyRegion(SDL_Display, SDL_Window, region, DRI2BufferFrontLeft, DRI2BufferBackLeft);
	XFixesDestroyRegion(SDL_Display, region);

	dri2_buf = -1; /* Preemptively invalidate buffers. */

#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	/* Forcefully throttle to something sensible; otherwise, we DoS X11. */
	Uint32 now = SDL_GetTicks();
	const int delay = dri2_accel ? 16 : 30; /* 16ms delay per frame ~= 63fps */
	while (now < dri2_last_swap + delay) {
		SDL_Delay(dri2_last_swap + delay - now);
		now = SDL_GetTicks();
	}
	dri2_last_swap = now;
#endif
}
#endif

/* Various screen update functions available */
static void X11_NormalUpdate(_THIS, int numrects, SDL_Rect *rects);
static void X11_MITSHMUpdate(_THIS, int numrects, SDL_Rect *rects);

int X11_SetupImage(_THIS, SDL_Surface *screen)
{
#ifndef NO_SHARED_MEMORY
	try_mitshm(this, screen);
	if(use_mitshm) {
		SDL_Ximage = XShmCreateImage(SDL_Display, SDL_Visual,
					     this->hidden->depth, ZPixmap,
					     shminfo.shmaddr, &shminfo, 
					     screen->w, screen->h);
		if(!SDL_Ximage) {
			XShmDetach(SDL_Display, &shminfo);
			XSync(SDL_Display, False);
			shmdt(shminfo.shmaddr);
			screen->pixels = NULL;
			goto error;
		}
		this->UpdateRects = X11_MITSHMUpdate;
	}
	if(!use_mitshm)
#endif /* not NO_SHARED_MEMORY */
	{
		int bpp;
		screen->pixels = SDL_malloc(screen->h*screen->pitch);
		if ( screen->pixels == NULL ) {
			SDL_OutOfMemory();
			return -1;
		}
 	        bpp = screen->format->BytesPerPixel;
		SDL_Ximage = XCreateImage(SDL_Display, SDL_Visual,
					  this->hidden->depth, ZPixmap, 0,
					  (char *)screen->pixels, 
					  screen->w, screen->h,
					  32, 0);
		if ( SDL_Ximage == NULL )
			goto error;
		/* XPutImage will convert byte sex automatically */
		SDL_Ximage->byte_order = (SDL_BYTEORDER == SDL_BIG_ENDIAN)
			                 ? MSBFirst : LSBFirst;
		this->UpdateRects = X11_NormalUpdate;
	}
	screen->pitch = SDL_Ximage->bytes_per_line;
	return(0);

error:
	SDL_SetError("Couldn't create XImage");
	return 1;
}

void X11_DestroyImage(_THIS, SDL_Surface *screen)
{
	if (screen->flags & SDL_HWSURFACE) {
#if SDL_VIDEO_DRIVER_X11_DRI2
		DRI2DestroyDrawable(SDL_Display, SDL_Window);
		X11_DRI2_InvalidateCache(this);
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
		X11_PVR2D_DestroyImage(this, screen);
		X11_PVR2D_DecRef(this);
#endif
		screen->flags &= ~(SDL_HWSURFACE|SDL_DOUBLEBUF);
#endif
	}
	if ( SDL_Ximage ) {
		XDestroyImage(SDL_Ximage);
#ifndef NO_SHARED_MEMORY
		if ( use_mitshm ) {
			XShmDetach(SDL_Display, &shminfo);
			XSync(SDL_Display, False);
			shmdt(shminfo.shmaddr);
		}
#endif /* ! NO_SHARED_MEMORY */
		SDL_Ximage = NULL;
	}
	if ( screen ) {
		screen->pixels = NULL;
	}
}

/* Determine the number of CPUs in the system */
static int num_CPU(void)
{
       static int num_cpus = 0;

       if(!num_cpus) {
#if defined(__LINUX__)
           char line[BUFSIZ];
           FILE *pstat = fopen("/proc/stat", "r");
           if ( pstat ) {
               while ( fgets(line, sizeof(line), pstat) ) {
                   if (SDL_memcmp(line, "cpu", 3) == 0 && line[3] != ' ') {
                       ++num_cpus;
                   }
               }
               fclose(pstat);
           }
#elif defined(__IRIX__)
	   num_cpus = sysconf(_SC_NPROC_ONLN);
#elif defined(_SC_NPROCESSORS_ONLN)
	   /* number of processors online (SVR4.0MP compliant machines) */
           num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
#elif defined(_SC_NPROCESSORS_CONF)
	   /* number of processors configured (SVR4.0MP compliant machines) */
           num_cpus = sysconf(_SC_NPROCESSORS_CONF);
#endif
           if ( num_cpus <= 0 ) {
               num_cpus = 1;
           }
       }
       return num_cpus;
}

int X11_ResizeImage(_THIS, SDL_Surface *screen, Uint32 flags)
{
	int retval;

	X11_DestroyImage(this, screen);
        if ( flags & (SDL_OPENGL|SDL_OPENGLES) ) {  /* No image when using GL */
        	retval = 0;
#if SDL_VIDEO_DRIVER_X11_DRI2
        } else if (flags & SDL_HWSURFACE) {
		DRI2CreateDrawable(SDL_Display, SDL_Window);
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
		retval = X11_PVR2D_AddRef(this);
#else
		SDL_SetError("No available direct rendering manager to use with DRI");
		retval = 1;
#endif
		if (retval == 0) {
			screen->flags |= flags & (SDL_HWSURFACE|SDL_DOUBLEBUF);
			this->UpdateRects = X11_DRI2_Update;
			dri2_buf = -1;
		}
#endif
        } else {
		retval = X11_SetupImage(this, screen);
		/* We support asynchronous blitting on the display */
		if ( flags & SDL_ASYNCBLIT ) {
			/* This is actually slower on single-CPU systems,
			   probably because of CPU contention between the
			   X server and the application.
			   Note: Is this still true with XFree86 4.0?
			*/
			if ( num_CPU() > 1 ) {
				screen->flags |= SDL_ASYNCBLIT;
			}
		}
	}
	return(retval);
}

/* We don't actually allow hardware surfaces other than the main one */
int X11_AllocHWSurface(_THIS, SDL_Surface *surface)
{
	if (!this->info.hw_available) {
		SDL_SetError("Hardware surfaces are not available");
		return -1;
	}
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	int ret = X11_PVR2D_AddRef(this);
	if (ret != 0) {
		return ret;
	}
	return X11_PVR2D_AllocSurface(this, surface);
#endif
	return(-1);
}

void X11_FreeHWSurface(_THIS, SDL_Surface *surface)
{
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	X11_PVR2D_FreeSurface(this, surface);
	X11_PVR2D_DecRef(this);
#endif
}

int X11_LockHWSurface(_THIS, SDL_Surface *surface)
{
#if SDL_VIDEO_DRIVER_X11_DRI2
	if (surface->flags & SDL_HWSURFACE) {
		if (surface == SDL_VideoSurface) {
			return X11_DRI2_LockVideoSurface(this, surface);
		} else {
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
			return X11_PVR2D_LockHWSurface(this, surface);
#else
			return -1;
#endif
		}
	}
#endif
	if ( (surface == SDL_VideoSurface) && blit_queued ) {
		XSync(GFX_Display, False);
		blit_queued = 0;
	}
	return(0);
}
void X11_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
#if SDL_VIDEO_DRIVER_X11_DRI2
	if (surface->flags & SDL_HWSURFACE) {
		if (surface == SDL_VideoSurface) {
			X11_DRI2_UnlockVideoSurface(this, surface);
		} else {
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
			X11_PVR2D_UnlockHWSurface(this, surface);
#endif
		}
	}
#endif
}

int X11_FlipHWSurface(_THIS, SDL_Surface *surface)
{
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	/* This will ensure GetBuffers has been called at least once. */
	int r = X11_DRI2_PrepareVideoSurface(this);
	if (r != 0) return r;
#endif
#if SDL_VIDEO_DRIVER_X11_DRI2
	CARD64 unused;
	DRI2SwapBuffers(SDL_Display, SDL_Window, 0, 0, 0, &unused);
	dri2_buf = -1; /* Preemptively invalidate buffers. */
#endif
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	/* Forcefully throttle to something sensible if we are not vsynced. */
	/* We do this AFTER swapping because in truth there's no doublebuf if we are
	   not accelerated; sleeping here gives time to the server/compositor
	   for doing its job and copying what was posted in the previous swapbuffers
	   call, reducing tearing. */
	if (!dri2_accel) {
		Uint32 now = SDL_GetTicks();
		const int delay = 30; /* 30ms delay per frame ~= 30fps */
		while (now < dri2_last_swap + delay) {
			SDL_Delay(dri2_last_swap + delay - now);
			now = SDL_GetTicks();
		}
		dri2_last_swap = now;

		/* Whether this actually does something I'm yet to see. */
		X11_PVR2D_WaitFlip(this);
	}
#endif
	return(0);
}

int X11_SetHWColorKey(_THIS, SDL_Surface *surface, Uint32 key)
{
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	/* Nothing to do; blitter will take care of this. */
	return 0;
#else
	return -1;
#endif
}

int X11_SetHWAlpha(_THIS, SDL_Surface *surface, Uint8 alpha)
{
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	/* Nothing to do; blitter will take care of this. */
	return 0;
#else
	return -1;
#endif
}

int X11_CheckHWBlit(_THIS, SDL_Surface *src, SDL_Surface *dst)
{
	src->flags &= ~SDL_HWACCEL;

	if ( !(src->flags & SDL_HWSURFACE) || !(dst->flags & SDL_HWSURFACE) ) {
		/* Do not allow SW->HW blits for now. */
		return 0;
	}

#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	if ((src->w * src->h) < PVR2D_THRESHOLD_SIZE) return 0;
	if (dst == SDL_VideoSurface || src == SDL_VideoSurface) {
		if (X11_DRI2_PrepareVideoSurface(this) < 0) return 0;
		if (!dri2_accel) return 0;
	}
	src->flags |= SDL_HWACCEL;
	src->map->hw_blit = X11_PVR2D_HWBlit;
#else
	return 0;
#endif /* SDL_VIDEO_DRIVER_X11_DRI2_PVR2D */
}

int X11_CheckHWFill(_THIS, SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
	if (dst == SDL_VideoSurface) {
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
		if (X11_DRI2_PrepareVideoSurface(this) < 0) return 0;
		if ((dstrect->w * dstrect->h) < PVR2D_THRESHOLD_SIZE) return 0;
		return dri2_accel;
#else
		return 0;
#endif
	}
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	return 1;
#else
	return 0;
#endif
}

int X11_FillHWRect(_THIS, SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D
	return X11_PVR2D_FillHWRect(this, dst, dstrect, color);
#endif
	return 0;
}

static void X11_NormalUpdate(_THIS, int numrects, SDL_Rect *rects)
{
	int i;
	
	for (i = 0; i < numrects; ++i) {
		if ( rects[i].w == 0 || rects[i].h == 0 ) { /* Clipped? */
			continue;
		}
		XPutImage(GFX_Display, SDL_Window, SDL_GC, SDL_Ximage,
			  rects[i].x, rects[i].y,
			  rects[i].x, rects[i].y, rects[i].w, rects[i].h);
	}
	if ( SDL_VideoSurface->flags & SDL_ASYNCBLIT ) {
		XFlush(GFX_Display);
		blit_queued = 1;
	} else {
		XSync(GFX_Display, False);
	}
}

static void X11_MITSHMUpdate(_THIS, int numrects, SDL_Rect *rects)
{
#ifndef NO_SHARED_MEMORY
	int i;

	for ( i=0; i<numrects; ++i ) {
		if ( rects[i].w == 0 || rects[i].h == 0 ) { /* Clipped? */
			continue;
		}
		XShmPutImage(GFX_Display, SDL_Window, SDL_GC, SDL_Ximage,
				rects[i].x, rects[i].y,
				rects[i].x, rects[i].y, rects[i].w, rects[i].h,
									False);
	}
	if ( SDL_VideoSurface->flags & SDL_ASYNCBLIT ) {
		XFlush(GFX_Display);
		blit_queued = 1;
	} else {
		XSync(GFX_Display, False);
	}
#endif /* ! NO_SHARED_MEMORY */
}

/* There's a problem with the automatic refreshing of the display.
   Even though the XVideo code uses the GFX_Display to update the
   video memory, it appears that updating the window asynchronously
   from a different thread will cause "blackouts" of the window.
   This is a sort of a hacked workaround for the problem.
*/
static int enable_autorefresh = 1;

void X11_DisableAutoRefresh(_THIS)
{
	--enable_autorefresh;
}

void X11_EnableAutoRefresh(_THIS)
{
	++enable_autorefresh;
}

void X11_RefreshDisplay(_THIS)
{
	/* Don't refresh a display that doesn't have an image (like GL)
	   Instead, post an expose event so the application can refresh.
	 */
	if ( ! SDL_Ximage || (enable_autorefresh <= 0) ) {
		SDL_PrivateExpose();
		return;
	}
#ifndef NO_SHARED_MEMORY
	if ( this->UpdateRects == X11_MITSHMUpdate ) {
		XShmPutImage(SDL_Display, SDL_Window, SDL_GC, SDL_Ximage,
				0, 0, 0, 0, this->screen->w, this->screen->h,
				False);
	} else
#endif /* ! NO_SHARED_MEMORY */
	{
		XPutImage(SDL_Display, SDL_Window, SDL_GC, SDL_Ximage,
			  0, 0, 0, 0, this->screen->w, this->screen->h);
	}
	XSync(SDL_Display, False);
}

