/*
 * File: buffer.h
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

#ifndef	__MEMORY_H__
#define	__MEMORY_H__

#include <stddef.h>
#include <stdbool.h>

/* Define macros for easy usage (automatically perform casting to indirect void pointer) */
#define	bufferAllocate(buffer, bufferSize, purpose) \
	voidBufferAllocate((void **)buffer, bufferSize, purpose)
#define	bufferFree(buffer) \
	voidBufferFree((void **)buffer)
#define	bufferMakeRoom(buffer, maxBufferSize, bufferSize, requiredSize, incrementSize) \
	voidBufferMakeRoom((void **)buffer, maxBufferSize, bufferSize, requiredSize, incrementSize)

/*
 * Function: bufferAllocate
 * Parameters:
 *	buffer - buffer (indirect pointer)
 *	bufferSize - size of the buffer to allocate (in bytes)
 *	purpose - optional string specifying where the buffer is going to be used for (used in logging when failure occurs)
 * Returns: a boolean specifying if the buffer is allocated successfully
 *
 * Remarks:
 * If no buffer could be allocated, then 'buffer' is unchanged.
 */
bool voidBufferAllocate(void **buffer, size_t bufferSize, const char *purpose);

/*
 * Function: bufferFree
 * Parameters:
 *	buffer - buffer (indirect pointer), will be set to NULL on success
 * Returns: a boolean specifying if the buffer is freed successfully
 *
 * Remarks:
 * A (indirect) pointer to NULL will be discarded and 'true' will be returned.
 */
bool voidBufferFree(void **buffer);

/*
 * Function: bufferGetBuffersInUse
 * Returns: te number of buffers in use (buffers which have been allocated and have not yet been freed)
 */
int32_t bufferGetBuffersInUse();

/*
 * Function: bufferMakeRoom
 * Parameters:
 *	buffer - buffer (indirect pointer)
 *	maxBufferSize - maximum allowed size of content in buffer (in bytes)
 *	bufferSize - size of content in buffer (in bytes)
 *	requiredSize - additional size needed in buffer (in bytes)
 *	incrementSize - size of increments with which the buffer size grows (in bytes)
 * Returns: a boolean specifying if the buffer has enough room or enough room is created successfully
 *
 * Remarks:
 * If not enough room is available in the original buffer, a new and larger buffer is created. The original buffer content is copied
 * to the new buffer. No initialisation is done on the extra buffer room added.
 */
bool voidBufferMakeRoom(void **buffer, size_t *maxBufferSize, size_t bufferSize, size_t requiredSize, size_t incrementSize);

#endif	/* __MEMORY_H__ */
