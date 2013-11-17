/*
 * File: rtspresponse.c
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
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "rtspresponse.h"
#include "log.h"
#include "buffer.h"

/* Buffer size */
#define	RESPONSE_BUFFER_INITIAL_SIZE	1024
#define RESPONSE_BUFFER_INCREMENT_SIZE	512

/* Characters and strings in response */
#define	NEWLINE_CHARACTER		((uint8_t)'\n')
#define	SUBKEY_SEPARATOR_CHARACTER	((uint8_t)';')
#define SUBKEY_ASSIGNMENT_STRING	((uint8_t *)"=")
#define SUBKEY_ASSIGNMENT_STRING_SIZE	1
#define KEY_SEPARATOR_STRING		((uint8_t *)": ")
#define KEY_SEPARATOR_STRING_SIZE	2

/* Type definition for the RTSP request */
struct RTSPResponseStruct {
	uint8_t *responseBuffer;
	size_t responseBufferSize;
	size_t maxResponseBufferSize;
};

/* Logging component name */
static const char *LOG_COMPONENT_NAME = "rtspresponse.c";

static uint8_t *rtspResponseFindValueForKey(RTSPResponse *rtspResponse, const char *key, const char *subkey);

RTSPResponse *rtspResponseCreate() {
	RTSPResponse *rtspResponse;

	/* Create rtsp request structure */
	if(!bufferAllocate(&rtspResponse, sizeof(RTSPResponse), "RTSP response")) {
		return NULL;
	}

	/* Initialize structure */
	rtspResponse->responseBuffer = NULL;
	rtspResponse->responseBufferSize = 0;
	rtspResponse->maxResponseBufferSize = 0;

	return rtspResponse;
}

bool rtspResponseReceive(RTSPResponse *rtspResponse, NetworkConnection *networkConnection) {
	size_t receivedMessageSize;

	/* Allocate buffer (if needed) */
	if(rtspResponse->responseBuffer == NULL) {
		rtspResponse->maxResponseBufferSize = RESPONSE_BUFFER_INITIAL_SIZE;
		if(!bufferAllocate(&rtspResponse->responseBuffer, rtspResponse->maxResponseBufferSize, "RTSP response buffer")) {
			return false;
		}
	}
	rtspResponse->responseBufferSize = 0;

	/* Receive messages (multiple if initial buffer was not big enough) */
	do {
		if(!bufferMakeRoom(&rtspResponse->responseBuffer, &rtspResponse->maxResponseBufferSize, rtspResponse->responseBufferSize, RESPONSE_BUFFER_INCREMENT_SIZE, RESPONSE_BUFFER_INCREMENT_SIZE)) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot allocate memory to add extra content to receive message.");
			bufferFree(&rtspResponse->responseBuffer);
			rtspResponse->responseBufferSize = 0;
			return false;
		}
		if(!networkReceiveMessage(networkConnection, rtspResponse->responseBuffer + rtspResponse->responseBufferSize, rtspResponse->maxResponseBufferSize, &receivedMessageSize)) {
			return false;
		}
		rtspResponse->responseBufferSize += receivedMessageSize;

		/* Repeat reading messages if a full buffer is read and more data is available on a TCP connection (UDP packets are lost) */
	} while(rtspResponse->responseBufferSize == rtspResponse->maxResponseBufferSize
			&& networkGetConnectionType(networkConnection) == TCP_CONNECTION
			&& networkIsMessageAvailable(networkConnection));

	/* Write info from this message */
        logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Received RTSP response:\n%.*s", (int)rtspResponse->responseBufferSize, rtspResponse->responseBuffer);

	return true;
}

bool rtspResponseGetStatus(RTSPResponse *rtspResponse, int16_t *status) {
	int16_t intValue;

	/* Check buffer for presence of status info */
	if(rtspResponse->responseBuffer == NULL || rtspResponse->responseBufferSize < 12) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "RTSP Response does not contain (enough) buffer content to retrieve status.");
		return false;
	}

	/* Check protocol value */
	if(memcmp(rtspResponse->responseBuffer, "RTSP/", 5) != 0 || !isdigit(rtspResponse->responseBuffer[5]) || rtspResponse->responseBuffer[6] != '.' || !isdigit(rtspResponse->responseBuffer[7]) || rtspResponse->responseBuffer[8] != ' ') {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "RTSP Response does not contain correct protocol name and version. Expected \"RTSP/<digit>.<digit><space>\" found \"%.*s\"", 9, rtspResponse->responseBuffer);
		return false;
	}

	/* Retrieve status value */
	if(sscanf((char *)rtspResponse->responseBuffer + 9, "%" SCNi16, &intValue) != 1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read status value from RTSP response");
		return false;
	}
	*status = intValue;

	return true;
}

bool rtspResponseGetSequenceNumber(RTSPResponse *rtspResponse, uint32_t *sequenceNumber) {
	uint8_t *value;
	uint32_t uint32Value;

	/* Retrieve CSeq value */
	value = rtspResponseFindValueForKey(rtspResponse, "CSeq", NULL);
	if(value != NULL) {
		if(sscanf((char *)value, "%" SCNu32, &uint32Value) != 1) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read CSeq value from RTSP response");
			return false;
		}
		*sequenceNumber = uint32Value;
	} else {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "No CSeq value in RTSP response (continuing anyway)");
	}
	return true;
}

bool rtspResponseGetSession(RTSPResponse *rtspResponse, uint32_t *session) {
	uint8_t *value;
	uint32_t uint32Value;

	/* Retrieve Session value */
	value = rtspResponseFindValueForKey(rtspResponse, "Session", NULL);
	if(value == NULL) {
		return false;
	}

	/* Convert value to integer */
	if(sscanf((char *)value, "%" SCNx32, &uint32Value) != 1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read Session value from RTSP response");
		return false;
	}
	*session = uint32Value;

	return true;
}

bool rtspResponseGetServerPort(RTSPResponse *rtspResponse, int16_t *serverPort) {
	uint8_t *value;
	int16_t int16Value;

	/* Retrieve Transport:server_port value */
	value = rtspResponseFindValueForKey(rtspResponse, "Transport", "server_port");
	if(value == NULL) {
		return false;
	}

	/* Convert value to integer */
	if(sscanf((char *)value, "%" SCNi16, &int16Value) != 1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read Transport:server_port value from RTSP response");
		return false;
	}
	*serverPort = int16Value;

	return true;
}

/* TODO: change this into something more readable/maintainable */
bool rtspResponseGetAuthenticationResponse(RTSPResponse *rtspResponse, char *realmBuffer, uint32_t maxRealmBufferSize, uint32_t *realmBufferSize, char *nonceBuffer, uint32_t maxNonceBufferSize, uint32_t *nonceBufferSize) {
	char *value;
	char *endValue;
	char *realmValue;
	uint32_t realmValueSize;
	char *nonceValue;
	uint32_t nonceValueSize;
	uint32_t *fieldValueSize;
	char *tempValue;

	/* Retrieve WWW-Authenticate value */
	value = (char *)rtspResponseFindValueForKey(rtspResponse, "WWW-Authenticate", NULL);
	if(value == NULL) {
		return false;
	}

	/* Initialize pointers */
	realmValue = NULL;
	nonceValue = NULL;

	/* Check field values, decide length of line containing values */
	endValue = memchr(value, NEWLINE_CHARACTER, rtspResponse->responseBufferSize - (value - (char *)rtspResponse->responseBuffer));
	if(endValue == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "RTSP Response has field WWW-Authenticate with unknown content %.*s", rtspResponse->responseBufferSize - (value - (char *)rtspResponse->responseBuffer), value);
		return false;
	}

	/* Check for authentication method 'Digest' */
	if(endValue - value < 7 || memcmp(value, "Digest ", 7) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "RTSP Response has field WWW-Authenticate with unknown method %.*s", endValue - value, value);
		return false;
	}
	value += 7;	/* Continu after 'Digest ' (incl. space) */

	/* Check for realm and nonce value */
	while(value < endValue && (realmValue == NULL || nonceValue == NULL)) {

		/* Skip spaces */
		while(value < endValue && *value == ' ') {
			value++;
		}

		/* Check for field name */
		if(value < endValue) {

			/* Check for field 'realm' */
			if(endValue - value  >= 7 && memcmp(value, "realm=\"", 7) == 0) {
				value += 7;

				/* Keep start of value */
				realmValue = value;
				fieldValueSize = &realmValueSize;

			/* Check for field 'nonce' */
			} else if(endValue - value >= 7 && memcmp(value, "nonce=\"", 7) == 0) {
				value += 7;

				/* Keep start of value */
				nonceValue = value;
				fieldValueSize = &nonceValueSize;

			/* Skip unknown field */
			} else {
				tempValue = value;
				while(value < endValue && *value != '"') {	/* Check for " */
					value++;
				}
				if(value >= endValue || *(value - 1) != '=') {	/* Check if it was actually =" */
					logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Unknown field %.*s in WWW-Authenticate.", endValue - tempValue, tempValue);
					return false;
				}
				logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Unknown field %.*s found in WWW-Authenticate. Skipping the value.", value - tempValue, tempValue);

				/* Continue with field value (skip double quote) */
				value++;
				fieldValueSize = NULL;
			}

			/* Skip to end of field value */
			tempValue = value;	/* Keep this for measuring the size of the field value */
			while(value < endValue && *value != '"') {
				value++;
			}
			if(value >= endValue) {
				logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Incomplete field value (not properly terminated by a double quote).");
				return false;
			}

			/* Store field value size */
			if(fieldValueSize != NULL) {
				*fieldValueSize = value - tempValue;
			}

			/* Skip terminating double quote */
			value++;

			/* Skip any field separator characters (setting pointer value to start of next field or to 'endValue') */
			while(value < endValue && (*value == ' ' || *value == ',')) {
				value++;
			}
		}
	}

	/* Check if both realm and nonce are present */
	if(realmValue == NULL || nonceValue == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "RTSP Response incomplete, fields 'realm' or 'nonce' not present.");
		return false;
	}

	/* Check if provided buffers are big enough */
	if(maxRealmBufferSize < realmValueSize || maxNonceBufferSize < nonceValueSize) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "RTSP Response contains value(s) which are too long for buffer. Buffer for 'realm' is %" PRIu32 " and buffer for 'nonce' is %" PRIu32 ".", maxRealmBufferSize, maxNonceBufferSize);
		return false;
	}

	/* Copy content */
	memcpy(realmBuffer, realmValue, realmValueSize);
	*realmBufferSize = realmValueSize;
	memcpy(nonceBuffer, nonceValue, nonceValueSize);
	*nonceBufferSize = nonceValueSize;

	return true;
}

uint8_t *rtspResponseFindValueForKey(RTSPResponse *rtspResponse, const char *key, const char *subkey) {
	uint8_t *valueLocation;
	uint8_t *responseEnd;
	size_t keySize;
	size_t subkeySize;

	/* Check response content */
	if(rtspResponse->responseBuffer == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "No content in RTSP Response when trying to retrieve %s%s%s value.", key, subkey == NULL ? "" : ":", subkey == NULL ? "" : subkey);
		return false;
	}

	/* Initialize local variables for searching */
	keySize = strlen(key);
	if(subkey != NULL) {
		subkeySize = strlen(subkey);
	} else {
		subkeySize = 0;
	}

	/* Iterate over response */
	responseEnd = rtspResponse->responseBuffer + rtspResponse->responseBufferSize;
	valueLocation = rtspResponse->responseBuffer;
	while(valueLocation < responseEnd) {

		/* Find start of next key */
		while(valueLocation < responseEnd && *valueLocation != NEWLINE_CHARACTER) {
			valueLocation++;
		}
		while(valueLocation < responseEnd && *valueLocation == NEWLINE_CHARACTER) {
			valueLocation++;
		}

		/* Key found? (check for presence of "<key>: <value>". Assume value is at least one position long) */
		if(valueLocation + keySize + KEY_SEPARATOR_STRING_SIZE + 1 <= responseEnd && memcmp(valueLocation, key, keySize) == 0 && memcmp(valueLocation + keySize, KEY_SEPARATOR_STRING, KEY_SEPARATOR_STRING_SIZE) == 0) {

			/* Skip key and separator */
			valueLocation += keySize + KEY_SEPARATOR_STRING_SIZE;

			/* Search for subkey? */
			if(subkey != NULL) {

				while(valueLocation < responseEnd && *valueLocation != NEWLINE_CHARACTER) {

					/* Find start of next subkey */
					while(valueLocation < responseEnd && *valueLocation != SUBKEY_SEPARATOR_CHARACTER && *valueLocation != NEWLINE_CHARACTER) {
						valueLocation++;
					}
					while(valueLocation < responseEnd && *valueLocation == SUBKEY_SEPARATOR_CHARACTER) {
						valueLocation++;
					}

					/* Subkey found? (check for presence of "<subkey>=<value>" or "<subkey>;". Assume value is at least one position long) */
					if(valueLocation + subkeySize <= responseEnd && memcmp(valueLocation, subkey, subkeySize) == 0) {

						/* Check for "<subkey>=<value>" */
						if(valueLocation + subkeySize + SUBKEY_ASSIGNMENT_STRING_SIZE + 1 <= responseEnd && memcmp(valueLocation + subkeySize, SUBKEY_ASSIGNMENT_STRING, SUBKEY_ASSIGNMENT_STRING_SIZE) == 0) {
							valueLocation += subkeySize + SUBKEY_ASSIGNMENT_STRING_SIZE;
							return valueLocation;
						}
						/* Check for "<subkey>;" (the location of the ; will be answered) */
						if(valueLocation + subkeySize <= responseEnd && *(valueLocation + subkeySize) == SUBKEY_SEPARATOR_CHARACTER) {
							valueLocation += subkeySize;
							return valueLocation;
						}
					}
				}
			} else {

				/* No subkey, so found value. Answer its location */
				return valueLocation;
			}
		}
	}

	return NULL;
}

bool rtspResponseFree(RTSPResponse **rtspResponse) {
	bool result;

	/* Close all opened/allocated resource. Continu if a failure occurs, but remember failure for final result. */
	result = true;
	if(*rtspResponse != NULL) {
		if(!bufferFree(&(*rtspResponse)->responseBuffer)) {
			result = false;
		}
		if(!bufferFree(rtspResponse)) {
			result = false;
		}
	}

	return result;
}
