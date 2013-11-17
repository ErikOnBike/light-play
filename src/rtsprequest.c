/*
 * File: rtsprequest.c
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

#include <stdlib.h>
#include <string.h>
#include "rtsprequest.h"
#include "log.h"
#include "buffer.h"

/* Buffer size */
#define	HEADER_BUFFER_INITIAL_SIZE	1024
#define HEADER_BUFFER_INCREMENT_SIZE	512

/* Max length of a number */
#define	MAX_NUMBER_STRING_SIZE		11

/* Size of largest command (ie "SET_PARAMETER") */
#define MAX_COMMAND_STRING_SIZE		13

/* Define max size of URL ("RTSP://<ip-address>/<session-id>" in which <session-id> is fixed value "1") */
#define MAX_EXTRA_URL_STRING_SIZE	10
#define MAX_URL_STRING_SIZE		(MAX_ADDR_STRING_LENGTH + MAX_EXTRA_URL_STRING_SIZE)

/* Type definition for the RTSP request */
struct RTSPRequestStruct {
	RTSPRequestMethod requestMethod;
	uint8_t *headerBuffer;
	size_t headerBufferSize;
	size_t maxHeaderBufferSize;
	uint8_t *contentBuffer;
	size_t contentBufferSize;
};

/* Names of methods */
char *METHOD_NAMES[] = {
	"OPTIONS",
	"ANNOUNCE",
	"SETUP",
	"RECORD",
	"SET_PARAMETER",
	"FLUSH",
	"TEARDOWN"
};

/* Logging component name */
static const char *LOG_COMPONENT_NAME = "rtsprequest.c";

RTSPRequest *rtspRequestCreate(RTSPRequestMethod requestMethod) {
	RTSPRequest *rtspRequest;

	/* Create rtsp request structure */
	if(!bufferAllocate(&rtspRequest, sizeof(RTSPRequest), "RTSP request")) {
		return NULL;
	}

	/* Initialize structure */
	rtspRequest->requestMethod = requestMethod;
	rtspRequest->headerBuffer = NULL;
	rtspRequest->headerBufferSize = 0;
	rtspRequest->maxHeaderBufferSize = 0;
	rtspRequest->contentBuffer = NULL;
	rtspRequest->contentBufferSize = 0;

	return rtspRequest;
}

bool rtspRequestReset(RTSPRequest *rtspRequest, RTSPRequestMethod requestMethod) {

	/* Initialize structure (keep buffer, no need to waste time freeing/allocating memory) */
	if(rtspRequest->headerBuffer != NULL) {
		rtspRequest->headerBuffer[0] = '\0';
		rtspRequest->headerBufferSize = 1;
	} else {
		rtspRequest->headerBufferSize = 0;
	}
	rtspRequest->contentBufferSize = 0;
	rtspRequest->requestMethod = requestMethod;

	return true;
}

const char *rtspRequestGetMethodName(RTSPRequest *rtspRequest) {
	return METHOD_NAMES[rtspRequest->requestMethod];
}

bool rtspRequestSend(RTSPRequest *rtspRequest, char *url, NetworkConnection *networkConnection) {
	uint8_t *requestBuffer;
	size_t maxRequestBufferSize;
	int charsWritten;

	/* Create buffer for full request (optimizer will get rid of all the individual constants) */
	maxRequestBufferSize = 12			/* "%s %s RTSP/1.0\r\n" printable characters */
		+ MAX_COMMAND_STRING_SIZE		/* command (first "%s" above) */
		+ MAX_URL_STRING_SIZE			/* url (second "%s" above) */
		+ rtspRequest->headerBufferSize - 1	/* header (excluding the terminating '\0') */
		+ 2					/* CR/LF */
		+ rtspRequest->contentBufferSize;	/* content */
	if(!bufferAllocate(&requestBuffer, maxRequestBufferSize, "RTSP request buffer")) {
		return false;
	}

	/* Write command */
	charsWritten = snprintf((char *)requestBuffer, maxRequestBufferSize, "%s %s RTSP/1.0\r\n", METHOD_NAMES[rtspRequest->requestMethod], rtspRequest->requestMethod == RTSP_METHOD_OPTIONS ? "*" : url);
	if(charsWritten < 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot write command to request buffer.");
		bufferFree(&requestBuffer);
		return false;
	}

	/* Validate if amount of buffer is still enough (see explanation above for the following calculation) */
	if(charsWritten + rtspRequest->headerBufferSize - 1 + 2 + rtspRequest->contentBufferSize > maxRequestBufferSize) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Request buffer is not big enough to hold header and content.");
		bufferFree(&requestBuffer);
		return false;
	}

	/* Write header fields */
	if(rtspRequest->headerBuffer != NULL) {
		memcpy(requestBuffer + charsWritten, rtspRequest->headerBuffer, rtspRequest->headerBufferSize - 1);	/* No need for '\0' so -1 */
		charsWritten += rtspRequest->headerBufferSize - 1;
	}

	/* Write header/content separator (length validation done above) */
	requestBuffer[charsWritten] = '\r';
	charsWritten++;
	requestBuffer[charsWritten] = '\n';
	charsWritten++;

	/* Write content */
	if(rtspRequest->contentBuffer != NULL) {
		memcpy(requestBuffer + charsWritten, rtspRequest->contentBuffer, rtspRequest->contentBufferSize);
		charsWritten += rtspRequest->contentBufferSize;
	}

	/* Send out request */
	if(!networkSendMessage(networkConnection, requestBuffer, charsWritten)) {
		bufferFree(&requestBuffer);
		return false;
	}

	/* Write info from this message */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Sent out RTSP request:\n%.*s", charsWritten, requestBuffer);

	/* Free up resources */
	if(!bufferFree(&requestBuffer)) {
		return false;
	}

	return true;
}

bool rtspRequestAddHeaderField(RTSPRequest *rtspRequest, const char *fieldName, const char *fieldValue) {
	size_t fieldNameLength;
	size_t fieldValueLength;
	int charsWritten;

	/* Allocate initial buffer if required */
	if(rtspRequest->headerBuffer == NULL) {
		rtspRequest->maxHeaderBufferSize = HEADER_BUFFER_INITIAL_SIZE;
		if(!bufferAllocate(&rtspRequest->headerBuffer, rtspRequest->maxHeaderBufferSize, "RTSP request header buffer")) {
			return false;
		}
		rtspRequest->headerBuffer[0] = '\0';
		rtspRequest->headerBufferSize = 1;	/* The '\0' byte */
	}

	/* Decide if enough space is available in buffer and add space if necessary. Add 4 bytes for ": " and "\r\n". */
	fieldNameLength = strlen(fieldName);
	fieldValueLength = strlen(fieldValue);
	if(!bufferMakeRoom(&rtspRequest->headerBuffer, &rtspRequest->maxHeaderBufferSize, rtspRequest->headerBufferSize, fieldNameLength + fieldValueLength + 4, HEADER_BUFFER_INCREMENT_SIZE)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot allocate memory to add field \"%s\" to RTSP Request header.", fieldName);
		bufferFree(&rtspRequest->headerBuffer);
		rtspRequest->headerBuffer = NULL;
		rtspRequest->headerBufferSize = 0;
		return false;
	}

	/* Add field name and value to buffer */
	/* Offet (headerBufferSize - 1) to overwrite existing '\0' byte. A new '\0' byte will be added at the end. */
	charsWritten = snprintf((char *)rtspRequest->headerBuffer + rtspRequest->headerBufferSize - 1, rtspRequest->maxHeaderBufferSize - (rtspRequest->headerBufferSize - 1), "%s: %s\r\n", fieldName, fieldValue);
	if(charsWritten != fieldNameLength + fieldValueLength + 4) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot add field \"%s\" to RTSP Request header.", fieldName);
		return false;
	}

	/* Update buffer data */
	rtspRequest->headerBufferSize += charsWritten;

	return true;
}

bool rtspRequestSetContent(RTSPRequest *rtspRequest, uint8_t *content, size_t contentSize, char *contentType) {
	char contentSizeString[MAX_NUMBER_STRING_SIZE];

	/* Add header field for content type */
	if(!rtspRequestAddHeaderField(rtspRequest, "Content-Type", contentType)) {
		return false;
	}

	/* Add header field for content size */
	sprintf(contentSizeString, "%lu", (unsigned long)contentSize);
	if(!rtspRequestAddHeaderField(rtspRequest, "Content-Length", contentSizeString)) {
		return false;
	}

	/* Free any existing buffer */
	if(rtspRequest->contentBuffer != NULL) {
		if(!bufferFree(&rtspRequest->contentBuffer)) {
			return false;
		}
	}

	/* Allocate buffer */
	if(!bufferAllocate(&rtspRequest->contentBuffer, contentSize, "RTSP request content buffer")) {
		return false;
	}

	/* Copy buffer */
	memcpy(rtspRequest->contentBuffer, content, contentSize);
	rtspRequest->contentBufferSize = contentSize;

	return true;
}

bool rtspRequestFree(RTSPRequest **rtspRequest) {
	bool result;

	/* Close all opened/allocated resource. Continu if a failure occurs, but remember failure for final result. */
	result = true;
	if(*rtspRequest != NULL) {
		if(!bufferFree(&(*rtspRequest)->headerBuffer)) {
			result = false;
		}
		if(!bufferFree(&(*rtspRequest)->contentBuffer)) {
			result = false;
		}
		if(!bufferFree(rtspRequest)) {
			result = false;
		}
	}

	return result;
}
