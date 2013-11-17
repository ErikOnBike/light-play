/*
 * File: rtsprequest.h
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

#ifndef	__RTSPREQUEST_H__
#define	__RTSPREQUEST_H__

#include <stddef.h>
#include <inttypes.h>
#include <stdbool.h>
#include "network.h"

typedef struct RTSPRequestStruct RTSPRequest;

/* Type definition for request methods */
typedef enum {
	RTSP_METHOD_OPTIONS = 0,
	RTSP_METHOD_ANNOUNCE,
	RTSP_METHOD_SETUP,
	RTSP_METHOD_RECORD,
	RTSP_METHOD_SET_PARAMETER,
	RTSP_METHOD_FLUSH,
	RTSP_METHOD_TEARDOWN
} RTSPRequestMethod;

/*
 * Function: rtspRequestCreate
 * Parameters:
 *	requestMethod - RTSP request method (see RTSPRequestMethod enum)
 * Returns: RTSPRequest structure
 */
RTSPRequest *rtspRequestCreate(RTSPRequestMethod requestMethod);

/*
 * Function: rtspRequestReset
 * Parameters:
 *	rtspRequest - already created RTSP Request (as returned by rtspRequestCreate)
 *	requestMethod - RTSP request method (see RTSPRequestMethod enum)
 * Returns: a boolean specifying if the message is reset successfully
 */
bool rtspRequestReset(RTSPRequest *rtspRequest, RTSPRequestMethod requestMethod);

/*
 * Function: rtspRequestGetMethodName
 * Parameters:
 *	rtspRequest - already created RTSP Request (as returned by rtspRequestCreate)
 * Returns: the name of the request method ('\0' terminated string)
 */
const char *rtspRequestGetMethodName(RTSPRequest *rtspRequest);

/*
 * Function: rtspRequestSend
 * Parameters:
 *	rtspRequest - already created RTSP Request (as returned by rtspRequestCreate)
 *	url - URL parameter of request
 *	networkConnection - network-connection the request-message is sent over
 * Returns: a boolean specifying if the message is sent successfully
 */
bool rtspRequestSend(RTSPRequest *rtspRequest, char *url, NetworkConnection *networkConnection);

/*
 * Function: rtspRequestAddHeaderField
 * Parameters:
 *	rtspRequest - already created RTSP Request (as returned by rtspRequestCreate)
 *	fieldName - name of the field
 *	fieldValue - value of the field (as array of characters)
 * Returns: a boolean specifying if the header field is added successfully
 */
bool rtspRequestAddHeaderField(RTSPRequest *rtspRequest, const char *fieldName, const char *fieldValue);

/*
 * Function: rtspRequestSetContent
 * Parameters:
 *      rtspRequest - already created RTSP Request (as returned by rtspRequestCreate)
 *      content - content (as array of bytes)
 *      contentSize - size of the content (as number of bytes)
 *      contentType - type of content as MIME-type
 * Returns: a boolean specifying if the content is set successfully
 */
bool rtspRequestSetContent(RTSPRequest *rtspRequest, uint8_t *content, size_t contentSize, char *contentType);

/*
 * Function: rtspRequestFree
 * Parameters:
 *	rtspRequest - already created RTSP Request (as returned by rtspRequestCreate)
 * Returns: a boolean specifying if the request is freed successfully
 *
 * Remarks:
 * This function will make the RTSP Request pointer NULL, so a freed request cannot be reused.
 */
bool rtspRequestFree(RTSPRequest **rtspRequest);

#endif	/* __RTSPREQUEST_H__ */
