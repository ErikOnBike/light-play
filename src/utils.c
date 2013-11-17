/*
 * File: utils.c
 *
 * Copyright (C) 2013 Erik Stel <erik.stel@gmail.com>
 *
 * This file is part of light-play.
 *
 * light-play is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * light-play is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with light-play.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <time.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include "log.h"
#include "utils.h"

#define	ONE_SECOND_IN_NANO_SECONDS	1000000000L

/* Logging component name */
static const char *LOG_COMPONENT_NAME = "utils.c";

void timespecInitialize(struct timespec *time) {
	time->tv_sec = 0;
	time->tv_nsec = 0;
}

void timespecCopy(struct timespec *destTime, const struct timespec *srcTime) {
	destTime->tv_sec = srcTime->tv_sec;
	destTime->tv_nsec = srcTime->tv_nsec;
}

void timespecAdd(struct timespec *time, const struct timespec *delta) {
	time->tv_sec += delta->tv_sec;
	time->tv_nsec += delta->tv_nsec;
	if(time->tv_nsec >= ONE_SECOND_IN_NANO_SECONDS) {
		time->tv_sec++;
		time->tv_nsec -= ONE_SECOND_IN_NANO_SECONDS;
	}
}

void timespecSubtract(const struct timespec *time1, const struct timespec *time2, struct timespec *delta) {
	if(time1->tv_sec >= time2->tv_sec) {
		if(time1->tv_nsec >= time2->tv_nsec) {
			delta->tv_sec = time1->tv_sec - time2->tv_sec;
			delta->tv_nsec = time1->tv_nsec - time2->tv_nsec;
		} else {
			/* time1->tv_nsec < time2->tv_nsec (we can not simply subtract) */
			if(time1->tv_sec > time2->tv_sec) {
				delta->tv_sec = time1->tv_sec - time2->tv_sec - 1; /* 'Borrow' 1 second because too few nanoseconds */
				delta->tv_nsec = ONE_SECOND_IN_NANO_SECONDS + time1->tv_nsec - time2->tv_nsec;  /* Use the borrowed second */
			} else {
				/* time1 < time2 */
				delta->tv_sec = 0;
				delta->tv_nsec = 0;
			}
		}
	} else {
		/* time1 < time2 */
		delta->tv_sec = 0;
		delta->tv_nsec = 0;
	}
}

bool getRandomNumber(uint32_t *randomValue) {
	static bool isSeeded = false;	/* Not thread safe, but unimportant here */
	struct timespec timeSpec;

	/* Initialize (pseudo)random number generator with a clock based seed */
	if(!isSeeded) {
		if(clock_gettime(CLOCK_MONOTONIC, &timeSpec) != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read the internal clock for using it in generating (pseudo)random numbers");
			return false;
		}
		if(timeSpec.tv_sec + timeSpec.tv_nsec == 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Clock returned value 0 which cannot be used for generating (pseudo)random numbers");
			return false;
		}
		srand((unsigned int)(timeSpec.tv_sec + timeSpec.tv_nsec));
		isSeeded = true;
	}

	/* Generate (pseudo)random number (handle 2 situations special: sizeof(int) == sizeof(uint32_t) and 2 * sizeof(int) == sizeof(uint32_t)) */
	if(sizeof(int) == sizeof(uint32_t)) {
		*randomValue = rand();
	} else if(2 * sizeof(int) == sizeof(uint32_t)) {
		*randomValue = ((uint32_t)rand()) << 16 && rand();
	} else {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Size of 'int' is unexpected and therefore (pseudo)random numbers can be less strong");
		*randomValue = (uint32_t)rand();
	}

	return true;
}

/* clock_gettime is not implemented on OS X, do simple implementation here (less accurate, but usable) */
#ifdef __MACH__
#include <sys/time.h>

int clock_gettime(int clk_id, struct timespec* tp) {
	struct timeval now;
	int result;

	if(clk_id != CLOCK_MONOTONIC) {
		return EINVAL;
	}

	/* Get time of day */
	result = gettimeofday(&now, NULL);
	if(result != 0) {
		return result;
	}

	/* Extract appropriate fields */
	tp->tv_sec  = now.tv_sec;
	tp->tv_nsec = now.tv_usec * 1000;

	return 0;
}
#endif	/* __MACH__ */
