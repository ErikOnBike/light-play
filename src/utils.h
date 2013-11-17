/*
 * File: utils.h
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

#ifndef	__UTILS_H__
#define	__UTILS_H__

#include <stddef.h>
#include <stdbool.h>

/*
 * Function: timespecInitialize
 * Parameters:
 *	time - time to initialize (to 0 seconds and 0 nanoseconds)
 */
void timespecInitialize(struct timespec *time);

/*
 * Function: timespecCopy
 * Parameters:
 *	destTime - destination where time will be copied to
 *	srcTime - source where time will be copied from
 */
void timespecCopy(struct timespec *destTime, const struct timespec *srcTime);

/*
 * Function: timespecAdd
 * Parameters:
 *	time - time where delta will be added to
 *	delta - delta time
 */
void timespecAdd(struct timespec *time, const struct timespec *delta);

/*
 * Function: timespecSubtract
 * Parameters:
 *	time1 - time
 *	time2 - time
 *	delta - difference between time1 and time2 (will be set to 0:0 in case time2 > time1)
 */
void timespecSubtract(const struct timespec *time1, const struct timespec *time2, struct timespec *delta);

/* clock_gettime is not implemented on OS X, only support for CLOCK_MONOTONIC */
#ifdef __MACH__
#include <sys/time.h>

#define CLOCK_MONOTONIC		0

int clock_gettime(int clk_id, struct timespec* tp);

#endif	/* __MACH__ */

#endif	/* __UTILS_H__ */
