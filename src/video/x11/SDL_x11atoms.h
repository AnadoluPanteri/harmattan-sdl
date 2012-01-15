/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2009 Sam Lantinga

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

SDL_X11_ATOM(WM_DELETE_WINDOW, "WM_DELETE_WINDOW")
SDL_X11_ATOM(WM_STATE, "WM_STATE")
SDL_X11_ATOM(_NET_WM_NAME, "_NET_WM_NAME")
SDL_X11_ATOM(_NET_WM_ICON_NAME, "_NET_WM_ICON_NAME")
SDL_X11_ATOM(_NET_WM_PID, "_NET_WM_PID")
SDL_X11_ATOM(_NET_WM_PING, "_NET_WM_PING")
SDL_X11_ATOM(_NET_WM_STATE, "_NET_WM_STATE")
SDL_X11_ATOM(_NET_WM_STATE_FULLSCREEN, "_NET_WM_STATE_FULLSCREEN")

#if SDL_VIDEO_DRIVER_X11_XINPUT2
SDL_X11_ATOM(AbsMTTrackingID, "Abs MT Tracking ID")
SDL_X11_ATOM(AbsMTPositionX, "Abs MT Position X")
SDL_X11_ATOM(AbsMTPositionY, "Abs MT Position Y")
#endif /* SDL_VIDEO_DRIVER_X11_XINPUT2 */

