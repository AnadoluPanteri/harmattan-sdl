/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2004 Sam Lantinga
	Copyright (C) 2010 Palm, Inc.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

/*
	ARM assembly mix routines
	palm, inc.
*/

#if defined(__ARM_NEON__) && defined(__GNUC__)
void SDL_MixAudio_ARM_NEON_S16LSB(Uint8 *dst, const Uint8 *src, Uint32 len, int volume);
#endif

#if 0
void SDL_MixAudio_ARMv6_S16LSB(Uint8 *dst, const Uint8 *src, Uint32 len, int volume);
#endif

