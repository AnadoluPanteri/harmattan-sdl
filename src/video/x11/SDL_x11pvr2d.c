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
#include "SDL_config.h"

#if SDL_VIDEO_DRIVER_X11_DRI2_PVR2D

#include "SDL_x11pvr2d_c.h"

/* This is shared between all surfaces. */
PVR2DCONTEXTHANDLE pvr2d_ctx = NULL;
static unsigned int pvr2d_refcount = 0;
static int pvr2d_fd;

struct pvr2dhwdata {
	PVR2DMEMINFO *info;
	PVR2DFORMAT format;
};

/* Not multithreading safe; this should only be called from rendering thread. */

int X11_PVR2D_AddRef(_THIS)
{
	if (pvr2d_refcount == 0) {
		/* Initialize PVR2D; point to primary device. */
		int numDevs = PVR2DEnumerateDevices(NULL);
		if (numDevs <= 0) return -1;

		PVR2DDEVICEINFO *devs = malloc(sizeof(PVR2DDEVICEINFO) * numDevs);
		if (PVR2DEnumerateDevices(devs) != PVR2D_OK) {
			free(devs);
			return -1;
		}

		unsigned long dev_id = devs[0].ulDevID;
		free(devs);

		if (PVR2DCreateDeviceContext(dev_id, &pvr2d_ctx, 0) != PVR2D_OK) {
			return -1;
		}

		pvr2d_fd = PVR2DGetFileHandle(pvr2d_ctx);
		if (pvr2d_fd == -1) {
			return -1;
		}
	}
	pvr2d_refcount++;
	return 0;
}

int X11_PVR2D_DecRef(_THIS)
{
	if (pvr2d_refcount == 0) return -1;
	pvr2d_refcount--;
	if (pvr2d_refcount == 0) {
		/* Deinitialize PVR2D to conserve device memory. */
		PVR2DDestroyDeviceContext(pvr2d_ctx);
	}
	return 0;
}

void X11_PVR2D_WaitFlip(_THIS)
{
	PVR2DERROR err = PVR2DUpdateEventReq(pvr2d_ctx, 0, NULL);
	if (err != PVR2D_OK) {
		fprintf(stderr, "Failed to request a PVR2DFlipEvent\n");
		return;
	}
	static char buf[1024]; /* We couldn't care less about the contents of it. */
	read(pvr2d_fd, buf, sizeof(buf));
}

static PVR2DFORMAT X11_PVR2D_MapFormat(SDL_PixelFormat *format)
{
	switch (format->BitsPerPixel) {
		case 8:
			return PVR2D_PAL8;
		case 16:
			return PVR2D_RGB565;
		case 24:
			return PVR2D_RGB888;
		case 32:
			return PVR2D_ARGB8888;
		default:  /* This will cause errors when blitting instead of garbage. */
			return PVR2D_NO_OF_FORMATS;
	}
}

static int X11_PVR2D_HasAlphaChannel(PVR2DFORMAT format)
{
	return format == PVR2D_ARGB4444 || format == PVR2D_ARGB8888 ||
		format == PVR2D_ARGB1555 ||
		format == PVR2D_ALPHA8 || format == PVR2D_ALPHA4;
}

int X11_PVR2D_SetupImage(_THIS, SDL_Surface *screen)
{
	if (!screen->hwdata) {
		screen->hwdata = malloc(sizeof(struct pvr2dhwdata));
	}
	struct pvr2dhwdata * hwdata = (struct pvr2dhwdata *) screen->hwdata;
	if (dri2_accel) {
		hwdata->info = dri2_cache[dri2_buf].dev_mem;
		hwdata->format = X11_PVR2D_MapFormat(screen->format);
	}
	return 0;
}

void X11_PVR2D_DestroyImage(_THIS, SDL_Surface *screen)
{
	if (screen->hwdata) {
		free(screen->hwdata);
		screen->hwdata = NULL;
	}
}

void * X11_PVR2D_MapBuffer(_THIS, DRI2Buffer *buffer, void **buf)
{
	PVR2DERROR err;
	PVR2DMEMINFO **meminfo = (PVR2DMEMINFO**) buf;
	err = PVR2DMemMap(pvr2d_ctx, 0, (void*)buffer->name, meminfo);
	if (err != PVR2D_OK) {
		return NULL;
	}
	return (*meminfo)->pBase;
}

void X11_PVR2D_UnmapBuffer(_THIS, void *buf)
{
	PVR2DMEMINFO *meminfo = buf;
	PVR2DMemFree(pvr2d_ctx, meminfo);
}

void X11_PVR2D_WaitBlits(_THIS, SDL_Surface *surface)
{
	struct pvr2dhwdata * hwdata = (struct pvr2dhwdata *) surface->hwdata;
	PVR2DQueryBlitsComplete(pvr2d_ctx, hwdata->info, 1);
}

void X11_PVR2D_ClearCache(_THIS, SDL_Surface *surface)
{
	struct pvr2dhwdata * hwdata = (struct pvr2dhwdata *) surface->hwdata;
	PVR2DERROR err = PVR2DCacheFlushDRI(pvr2d_ctx, PVR2D_CFLUSH_TO_GPU,
		(unsigned long) hwdata->info->pBase, hwdata->info->ui32MemSize);
	printf("cache err = %d\n", err);
}

int X11_PVR2D_AllocSurface(_THIS, SDL_Surface *surface)
{
	PVR2DERROR err;

	surface->flags &= ~SDL_HWSURFACE;
	struct pvr2dhwdata * hwdata = (struct pvr2dhwdata *) SDL_malloc(sizeof(struct pvr2dhwdata));
	if (!hwdata) {
		SDL_OutOfMemory();
		return -1;
	}

	hwdata->format = X11_PVR2D_MapFormat(surface->format);
	if (hwdata->format == PVR2D_NO_OF_FORMATS) {
		SDL_SetError("Invalid surface pixel format for PVR2D");
		free(hwdata);
		return -1;
	}

	unsigned long bytes = surface->h * surface->pitch;
	err = PVR2DMemAlloc(pvr2d_ctx, bytes, PVR2D_ALIGNMENT_4, 0, &hwdata->info);
	if (err != PVR2D_OK) {
		SDL_SetError("PVR2D memory failure");
		free(hwdata);
		return -1;
	}

	surface->flags |= SDL_HWSURFACE;
	surface->hwdata = (void*) hwdata;

	return 0;
}

void X11_PVR2D_FreeSurface(_THIS, SDL_Surface *surface)
{
	struct pvr2dhwdata * hwdata = (struct pvr2dhwdata *) surface->hwdata;
	PVR2DERROR err = PVR2DMemFree(pvr2d_ctx, hwdata->info);
	if (err) {
		fprintf(stderr, "PVR2DMemFree error %d\n", err);
	}
	free(hwdata);
}

int X11_PVR2D_LockHWSurface(_THIS, SDL_Surface *surface)
{
	struct pvr2dhwdata * hwdata = (struct pvr2dhwdata *) surface->hwdata;
	X11_PVR2D_WaitBlits(this, surface);
	surface->pixels = hwdata->info->pBase;
	return 0;
}

void X11_PVR2D_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	surface->pixels = NULL;
}

int X11_PVR2D_FillHWRect(_THIS, SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
	struct pvr2dhwdata * hwdata = (struct pvr2dhwdata *) dst->hwdata;
	PVR2DERROR err;
	PVR2DBLTINFO blt = {0};
	Uint8 r, g, b, a;
	SDL_GetRGBA(color, dst->format, &r, &g, &b, &a);

	blt.CopyCode = PVR2DPATROPcopy;
	blt.Colour = ((a << 24) & 0xFF000000U) |
	             ((r << 16) & 0x00FF0000U) |
	             ((g << 8 ) & 0x0000FF00U) |
	             ((b      ) & 0x000000FFU);
	blt.pDstMemInfo = hwdata->info;
	blt.DstOffset = 0;
	blt.DstStride = dst->pitch;
	blt.DstX = dstrect->x;
	blt.DstY = dstrect->y;
	blt.DSizeX = dstrect->w;
	blt.DSizeY = dstrect->h;
	blt.DstFormat = hwdata->format;
	blt.DstSurfWidth = dst->w;
	blt.DstSurfHeight = dst->h;

	err = PVR2DBlt(pvr2d_ctx, &blt);
	if (err) {
		fprintf(stderr, "PVR2DBlt failed with err=%d\n", err);
		return -1;
	}
	return 0;
}

int X11_PVR2D_HWBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect)
{
	struct pvr2dhwdata * srcdata = (struct pvr2dhwdata *) src->hwdata;
	struct pvr2dhwdata * dstdata = (struct pvr2dhwdata *) dst->hwdata;
	PVR2DERROR err;
	PVR2DBLTINFO blt = {0};

	blt.CopyCode = PVR2DROPcopy;
	blt.BlitFlags = PVR2D_BLIT_DISABLE_ALL;
	blt.pDstMemInfo = dstdata->info;
	blt.DstOffset = 0;
	blt.DstStride = dst->pitch;
	blt.DstX = dstrect->x;
	blt.DstY = dstrect->y;
	blt.DSizeX = dstrect->w;
	blt.DSizeY = dstrect->h;
	blt.DstFormat = dstdata->format;

	blt.pSrcMemInfo = srcdata->info;
	blt.SrcOffset = 0;
	blt.SrcStride = src->pitch;
	blt.SrcX = srcrect->x;
	blt.SrcY = srcrect->y;
	blt.SizeX = srcrect->w;
	blt.SizeY = srcrect->h;
	blt.SrcFormat = srcdata->format;

	/* Here comes the implementation of the SDL_SetAlpha logic. */
	char src_has_alpha = X11_PVR2D_HasAlphaChannel(srcdata->format);
	char dst_has_alpha = X11_PVR2D_HasAlphaChannel(dstdata->format);
	char alpha_enabled = (src->flags & SDL_SRCALPHA) == SDL_SRCALPHA;
	char ckey_enabled = (src->flags & SDL_SRCCOLORKEY) == SDL_SRCCOLORKEY;

	if ( ckey_enabled && (!src_has_alpha || !alpha_enabled)) {
		Uint8 r, g, b, a;
		SDL_GetRGBA(src->format->colorkey, src->format, &r, &g, &b, &a);
		blt.ColourKey = ((a << 24) & 0xFF000000U) |
			         ((r << 16) & 0x00FF0000U) |
			         ((g << 8 ) & 0x0000FF00U) |
			         ((b      ) & 0x000000FFU);
		switch (srcdata->format) {
			case PVR2D_RGB565:
				blt.ColourKeyMask = CKEY_MASK_565;
				break;
			default:  /* Paletted is also rgba8888 here. */
				blt.ColourKeyMask = CKEY_MASK_8888;
				break;
		}
		blt.BlitFlags |= PVR2D_BLIT_CK_ENABLE | PVR2D_BLIT_COLKEY_SOURCE;
	}
	if ( alpha_enabled ) {
		if (src_has_alpha) {
			blt.AlphaBlendingFunc = PVR2D_ALPHA_OP_SRC_DSTINV;
			blt.BlitFlags |= PVR2D_BLIT_PERPIXEL_ALPHABLEND_ENABLE;
		} else {
			blt.GlobalAlphaValue = src->format->alpha;
			blt.BlitFlags |= PVR2D_BLIT_GLOBAL_ALPHA_ENABLE;
		}
	}

	err = PVR2DBlt(pvr2d_ctx, &blt);
	if (err) {
		fprintf(stderr, "PVR2DBlt failed with err=%d\n", err);
		return -1;
	}
	return 0;
}

#endif /* SDL_VIDEO_DRIVER_X11_DRI2_PVR2D */

