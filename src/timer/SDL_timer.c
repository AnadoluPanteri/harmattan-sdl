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

#include "SDL_timer.h"
#include "SDL_timer_c.h"
#include "SDL_mutex.h"
#include "SDL_systimer.h"

/* #define DEBUG_TIMERS */

int SDL_timer_started = 0;
int SDL_timer_running = 0;

/* Data to handle a single periodic alarm */
Uint32 SDL_alarm_interval = 0;
SDL_TimerCallback SDL_alarm_callback;

/* Data used for a thread-based timer */
static int SDL_timer_threaded = 0;

#if defined(HAVE_UNISTD_H) && defined(HAVE_FCNTL_H)
#define SDL_EVENT_WAKEUP_PIPE 1
#include <unistd.h> /* for pipe(),select() */
#include <fcntl.h>  /* for fcntl() */

static int wakeup_pipe[2] = { -1, -1 };
#endif

struct _SDL_TimerID {
	Uint32 interval;
	SDL_NewTimerCallback cb;
	void *param;
	Uint32 last_alarm;
	struct _SDL_TimerID *next;
};

static SDL_TimerID SDL_timers = NULL;
static SDL_mutex *SDL_timer_mutex;
static int list_changes = 0;

static void SDL_TimerChanged(void)
{
	list_changes++;
#ifdef SDL_EVENT_WAKEUP_PIPE
	if (wakeup_pipe[1] != -1) {
		write(wakeup_pipe[1], "T", 1);
	}
#endif
}

/* Set whether or not the timer should use a thread.
   This should not be called while the timer subsystem is running.
*/
int SDL_SetTimerThreaded(int value)
{
	int retval;

	if ( SDL_timer_started ) {
		SDL_SetError("Timer already initialized");
		retval = -1;
	} else {
		retval = 0;
		SDL_timer_threaded = value;
	}
	return retval;
}

int SDL_TimerInit(void)
{
	int retval;

	retval = 0;
	if ( SDL_timer_started ) {
		SDL_TimerQuit();
	}
#ifdef SDL_EVENT_WAKEUP_PIPE
	retval = pipe(wakeup_pipe);
	if (retval != 0) {
		return -1;
	}
	retval += fcntl(wakeup_pipe[0], F_SETFD, FD_CLOEXEC);
	retval += fcntl(wakeup_pipe[1], F_SETFD, FD_CLOEXEC);
	if (retval != 0) {
		return -1;
	}
#endif
	if ( ! SDL_timer_threaded ) {
		retval = SDL_SYS_TimerInit();
	}
	if ( SDL_timer_threaded ) {
		SDL_timer_mutex = SDL_CreateMutex();
	}
	if ( retval == 0 ) {
		SDL_timer_started = 1;
	}
	return(retval);
}

void SDL_TimerQuit(void)
{
	SDL_SetTimer(0, NULL);
#ifdef SDL_EVENT_WAKEUP_PIPE
	if (wakeup_pipe[1] != -1) {
		/* Ensure a possibly blocked timer thread is awaken. */
		write(wakeup_pipe[1], "Q", 1);
	}
#endif
	if ( SDL_timer_threaded < 2 ) {
		SDL_SYS_TimerQuit();
	}
	if ( SDL_timer_threaded ) {
		SDL_DestroyMutex(SDL_timer_mutex);
		SDL_timer_mutex = NULL;
	}
#ifdef SDL_EVENT_WAKEUP_PIPE
	close(wakeup_pipe[0]);
	close(wakeup_pipe[1]);
	wakeup_pipe[0] = -1;
	wakeup_pipe[1] = -1;
#endif
	SDL_timer_started = 0;
	SDL_timer_threaded = 0;
}

Uint32 SDL_ThreadedTimerCheck(void)
{
	Uint32 now, ms, wait;
	SDL_TimerID t, prev, next;
	SDL_bool removed;

	SDL_mutexP(SDL_timer_mutex);
	list_changes = 0;
	now = SDL_GetTicks();
	for ( prev = NULL, t = SDL_timers; t; t = next ) {
		removed = SDL_FALSE;
		ms = t->interval - SDL_TIMESLICE;
		next = t->next;
		if ( (int)(now - t->last_alarm) > (int)ms ) {
			struct _SDL_TimerID timer;

			if ( (now - t->last_alarm) < t->interval ) {
				t->last_alarm += t->interval;
			} else {
				t->last_alarm = now;
			}
#ifdef DEBUG_TIMERS
			printf("Executing timer %p (thread = %d)\n",
				t, SDL_ThreadID());
#endif
			timer = *t;
			SDL_mutexV(SDL_timer_mutex);
			ms = timer.cb(timer.interval, timer.param);
			SDL_mutexP(SDL_timer_mutex);
			if ( list_changes ) {
				/* Abort, list of timers modified */
				/* FIXME: what if ms was changed? */
				break;
			}
			if ( ms != t->interval ) {
				if ( ms ) {
					t->interval = ROUND_RESOLUTION(ms);
				} else {
					/* Remove timer from the list */
#ifdef DEBUG_TIMERS
					printf("SDL: Removing timer %p\n", t);
#endif
					if ( prev ) {
						prev->next = next;
					} else {
						SDL_timers = next;
					}
					SDL_free(t);
					--SDL_timer_running;
					removed = SDL_TRUE;
				}
			}
		}
		/* Don't update prev if the timer has disappeared */
		if ( ! removed ) {
			prev = t;
		}
	}
	wait = 0xFFFFFFFFUL;
	for ( t = SDL_timers; t; t = t->next ) {
		int dist = (int)t->interval - (int)(now - t->last_alarm);
		if (dist < 0) {
			wait = 0;
		} else if (dist < wait) {
			wait = dist;
		}
	}
	SDL_mutexV(SDL_timer_mutex);
	return wait;
}

SDL_bool SDL_ThreadedTimerIteration(void)
{
#ifdef SDL_EVENT_WAKEUP_PIPE
	while (list_changes > 0 && wakeup_pipe[0] != -1) {
			/* Read one character from the pipe. */
		char c;
		read(wakeup_pipe[0], &c, 1);
		list_changes--;
		if (c == 'Q') {
			return SDL_FALSE;
		}
	}
#endif

	Uint32 wait = SDL_ThreadedTimerCheck();

#ifdef SDL_EVENT_WAKEUP_PIPE
	struct timeval tv;
	int nfds;
	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(wakeup_pipe[0], &rfds);
	nfds = wakeup_pipe[0] + 1;

	if (wait == 0xFFFFFFFFUL) {
		/* Wait forever. */
	} else {
		tv.tv_sec = wait / 1000;
		tv.tv_usec = (wait % 1000) * 1000;
	}

	select(nfds, &rfds, NULL, NULL, wait == 0xFFFFFFFFUL ? NULL : &tv);
#else
	SDL_Delay(1);
#endif
	return SDL_TRUE;
}

static SDL_TimerID SDL_AddTimerInternal(Uint32 interval, SDL_NewTimerCallback callback, void *param)
{
	SDL_TimerID t;
	t = (SDL_TimerID) SDL_malloc(sizeof(struct _SDL_TimerID));
	if ( t ) {
		t->interval = ROUND_RESOLUTION(interval);
		t->cb = callback;
		t->param = param;
		t->last_alarm = SDL_GetTicks();
		t->next = SDL_timers;
		SDL_timers = t;
		++SDL_timer_running;
		SDL_TimerChanged();
	}
#ifdef DEBUG_TIMERS
	printf("SDL_AddTimer(%d) = %08x num_timers = %d\n", interval, (Uint32)t, SDL_timer_running);
#endif
	return t;
}

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_NewTimerCallback callback, void *param)
{
	SDL_TimerID t;
	if ( ! SDL_timer_mutex ) {
		if ( SDL_timer_started ) {
			SDL_SetError("This platform doesn't support multiple timers");
		} else {
			SDL_SetError("You must call SDL_Init(SDL_INIT_TIMER) first");
		}
		return NULL;
	}
	if ( ! SDL_timer_threaded ) {
		SDL_SetError("Multiple timers require threaded events!");
		return NULL;
	}
	SDL_mutexP(SDL_timer_mutex);
	t = SDL_AddTimerInternal(interval, callback, param);
	SDL_mutexV(SDL_timer_mutex);
	return t;
}

SDL_bool SDL_RemoveTimer(SDL_TimerID id)
{
	SDL_TimerID t, prev = NULL;
	SDL_bool removed;

	removed = SDL_FALSE;
	SDL_mutexP(SDL_timer_mutex);
	/* Look for id in the linked list of timers */
	for (t = SDL_timers; t; prev=t, t = t->next ) {
		if ( t == id ) {
			if(prev) {
				prev->next = t->next;
			} else {
				SDL_timers = t->next;
			}
			SDL_free(t);
			--SDL_timer_running;
			removed = SDL_TRUE;
			SDL_TimerChanged();
			break;
		}
	}
#ifdef DEBUG_TIMERS
	printf("SDL_RemoveTimer(%08x) = %d num_timers = %d thread = %d\n", (Uint32)id, removed, SDL_timer_running, SDL_ThreadID());
#endif
	SDL_mutexV(SDL_timer_mutex);
	return removed;
}

/* Old style callback functions are wrapped through this */
static Uint32 SDLCALL callback_wrapper(Uint32 ms, void *param)
{
	SDL_TimerCallback func = (SDL_TimerCallback) param;
	return (*func)(ms);
}

int SDL_SetTimer(Uint32 ms, SDL_TimerCallback callback)
{
	int retval;

#ifdef DEBUG_TIMERS
	printf("SDL_SetTimer(%d)\n", ms);
#endif
	retval = 0;

	if ( SDL_timer_threaded ) {
		SDL_mutexP(SDL_timer_mutex);
	}
	if ( SDL_timer_running ) {	/* Stop any currently running timer */
		if ( SDL_timer_threaded ) {
			while ( SDL_timers ) {
				SDL_TimerID freeme = SDL_timers;
				SDL_timers = SDL_timers->next;
				SDL_free(freeme);
			}
			SDL_timer_running = 0;
			SDL_TimerChanged();
		} else {
			SDL_SYS_StopTimer();
			SDL_timer_running = 0;
		}
	}
	if ( ms ) {
		if ( SDL_timer_threaded ) {
			if ( SDL_AddTimerInternal(ms, callback_wrapper, (void *)callback) == NULL ) {
				retval = -1;
			}
		} else {
			SDL_timer_running = 1;
			SDL_alarm_interval = ms;
			SDL_alarm_callback = callback;
			retval = SDL_SYS_StartTimer();
		}
	}
	if ( SDL_timer_threaded ) {
		SDL_mutexV(SDL_timer_mutex);
	}

	return retval;
}
