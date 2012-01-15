/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2009 Sam Lantinga

    This library is SDL_free software; you can redistribute it and/or
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
#ifndef _SDL_sensorfw_h
#define _SDL_sensorfw_h

#include "SDL_config.h"
#include "SDL_joystick.h"

extern int sdl_sfw_num_joysticks;

extern void SDL_SFW_Init(void);
extern void SDL_SFW_Quit(void);

extern const char *SDL_SFW_JoystickName(int index);
extern int SDL_SYS_JoystickOpen(SDL_Joystick *joystick);
extern void SDL_SYS_JoystickUpdate(SDL_Joystick *joystick);
extern void SDL_SYS_JoystickClose(SDL_Joystick *joystick);

#endif /* _SDL_sensorfw_h */
