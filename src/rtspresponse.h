/*
 * File: rtspresponse.h
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

#ifndef	__RTSPRESPONSE_H__
#define	__RTSPRESPONSE_H__

#include <stddef.h>
#include <inttypes.h>
#include <stdbool.h>
#include "network.h"

typedef struct RTSPResponseStruct RTSPResponse;

/*
 * Function: rtspResponseCreate
 * Returns: RTSPResponse structure
 */
RTSPResponse *rtspResponseCreate();

/*
 * Function: rtspResponseReceive
 * Parameters:
 *	rtspResponse - already created RTSP Response (as returned by rtspResponseCreate)
 *	networkConnection - network-connection the request-message is received from
 * Returns: a boolean specifying if the message is received successfully
 */
bool rtspResponseReceive(RTSPResponse *rtspResponse, NetworkConnection *networkConnection);

/*
 * Function: rtspResponseGetStatus
 * Parameters:
 *	rtspResponse - already created RTSP Response (as returned by rtspResponseCreate)
 *	status - status retrieved from response
 * Returns: a boolean specifying if the status is present and extracted successfully
 */
bool rtspResponseGetStatus(RTSPResponse *rtspResponse, int16_t *status);

/*
 * Function: rtspResponseGetSequenceNumber
 * Parameters:
 *	rtspResponse - already created RTSP Response (as returned by rtspResponseCreate)
 *	sequenceNumber - sequence number (key CSeq) retrieved from response
 * Returns: a boolean specifying if the sequence number is extracted successfully (will return true if none is present)
 */
bool rtspResponseGetSequenceNumber(RTSPResponse *rtspResponse, uint32_t *sequenceNumber);

/*
 * Function: rtspResponseGetSession
 * Parameters:
 *	rtspResponse - already created RTSP Response (as returned by rtspResponseCreate)
 *	session - session (key Session) retrieved from response
 * Returns: a boolean specifying if the session is present and extracted successfully
 */
bool rtspResponseGetSession(RTSPResponse *rtspResponse, uint32_t *session);

/*
 * Function: rtspResponseGetServerPort
 * Parameters:
 *	rtspResponse - already created RTSP Response (as returned by rtspResponseCreate)
 *	serverPort - server-port (key Transport:server_port) retrieved from response
 * Returns: a boolean specifying if the server-port is present and extracted successfully
 */
bool rtspResponseGetServerPort(RTSPResponse *rtspResponse, int16_t *serverPort);

/*
 * Function: rtspResponseGetAuthenticationResponse
 * Parameters:
 *	rtspResponse - already created RTSP Response (as returned by rtspResponseCreate)
 *	realmBuffer - buffer for realm retrieved from response
 *	maxRealmBufferSize - maximum size of the buffer
 *	realmeBufferSize - size of the buffer (actual size of content)
 *	nonceBuffer - buffer for nonce retrieved from response
 *	maxNonceBufferSize - maximum size of the buffer
 *	nonceBufferSize - size of the buffer (actual size of content)
 * Returns: a boolean specifying if the realm and nonce values are present and extracted successfully
 *
 * Remarks:
 * A nonce is only present in a response to a request when no authentication is done (yet), but is expected.
 * The content of the response is checked for a known Digest authentication. Unknown fields are skipped.
 */
bool rtspResponseGetAuthenticationResponse(RTSPResponse *rtspResponse, char *realmBuffer, uint32_t maxRealmBufferSize, uint32_t *realmBufferSize, char *nonceBuffer, uint32_t maxNonceBufferSize, uint32_t *nonceBufferSize);

/*
 * Function: rtspResponseFree
 * Parameters:
 *	rtspResponse - already created RTSP Response (as returned by rtspResponseCreate)
 * Returns: a boolean specifying if the response is freed successfully
 *
 * Remarks:
 * This function will make the RTSP Response pointer NULL, so a freed response cannot be reused.
 */
bool rtspResponseFree(RTSPResponse **rtspResponse);

#endif	/* __RTSPRESPONSE_H__ */
