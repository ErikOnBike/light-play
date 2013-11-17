/*
 * File: buffer.c
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "buffer.h"

/* Logging component name */
static const char *LOG_COMPONENT_NAME = "buffer.c";

static int32_t bufferAllocateCount = 0;

bool voidBufferAllocate(void **buffer, size_t bufferSize, const char *purpose) {
	void *newBuffer;

	/* Allocate memory */
	newBuffer = malloc(bufferSize);
	if(newBuffer == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot allocate memory (%lu bytes) for %s.", (unsigned long)bufferSize, purpose == NULL ? "a buffer" : purpose);
		return false;
	}

	/* Set result and increment bufferAllocateCount counter */
	*buffer = newBuffer;
	bufferAllocateCount++;

	return true;
}

bool voidBufferFree(void **buffer) {
	/* Free memory and decrement bufferAllocateCount counter */
	if(*buffer != NULL) {
		free(*buffer);
		*buffer = NULL;
		bufferAllocateCount--;
	}

	return true;
}

int32_t bufferGetBuffersInUse() {
	return bufferAllocateCount;
}

bool voidBufferMakeRoom(void **buffer, size_t *maxBufferSize, size_t bufferSize, size_t requiredSize, size_t incrementSize) {
	void *newBuffer;
	void *tmpBuffer;
	size_t newSize;

	/* Decide if buffer needs additional space */
	if(bufferSize + requiredSize <= *maxBufferSize) {
		return true;
	}

	/* Decide new size (assume we will not overflow the size_t value) */
	newSize = *maxBufferSize + incrementSize;
	while(bufferSize + requiredSize > newSize) {
		newSize += incrementSize;
	}

	/* Allocate new buffer and copy content */
	if(!bufferAllocate(&newBuffer, newSize, "a larger buffer")) {
		return false;
	}
	memcpy(newBuffer, *buffer, bufferSize);

	/* Swap new buffer in and set max size appropriately */
	tmpBuffer = *buffer;
	*buffer = newBuffer;
	*maxBufferSize = newSize;

	/* Clean up old buffer */
	return bufferFree(&tmpBuffer);
}
