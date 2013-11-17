/*
 * File: rtspclient.h
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

#ifndef	__RTSPCLIENT_H__
#define	__RTSPCLIENT_H__

#include <stddef.h>
#include <inttypes.h>
#include <stdbool.h>
#include "raopclient.h"
#include "rtsprequest.h"
#include "rtspresponse.h"

typedef struct RTSPClientStruct RTSPClient;

/*
 * Function: rtspClientOpenConnection
 * Parameters:
 *	hostName - name of host
 *	portName - name of port
 *      password - password (optional)
 * Returns: RTSP client structure.
 *
 * Remarks:
 * The portName parameter can either be the string representation of a port number but can also be a service name like "ftp" or "telnet".
 */
RTSPClient *rtspClientOpenConnection(const char *hostName, const char *portName, const char *password);

/*
 * Function: rtspClientSendCommand
 * Parameters:
 *      rtspClient - already open RTSP client connection (as returned by openConnection)
 *      requestMethod - method of the command to send
 *	raopClient - RAOP client
 *	raopClientContentSupplier - content supplier (from RAOP client) for the command to send (might be NULL)
 * Returns: a boolean specifying if the command was sent successfully
 *
 * Remarks:
 * If no special content is required, raopClientContentSupplier can be set to NULL.
 */
bool rtspClientSendCommand(RTSPClient *rtspClient, RTSPRequestMethod requestMethod, RAOPClient *raopClient, bool (*raopClientContentSupplier)(RAOPClient *raopClient, RTSPRequest *rtspRequest));

/*
 * Function: rtspClientGetLocalAddressName
 * Parameters:
 *      rtspClient - already open RTSP client connection (as returned by openConnection)
 *      addressName - array of characters where name of local address will be stored (local IP address)
 *      maxAddressNameSize - the size of the name buffer (as number of characters)
 * Returns: a boolean specifying if the name was retrieved successfully
 */
bool rtspClientGetLocalAddressName(RTSPClient *rtspClient, char *addressName, int maxAddressNameSize);

/*
 * Function: rtspClientGetRemoteAddressName
 * Parameters:
 *      rtspClient - already open RTSP client connection (as returned by openConnection)
 *      addressName - array of characters where name of remote address will be stored (remote IP address)
 *      maxAddressNameSize - the size of the name buffer (as number of characters)
 * Returns: a boolean specifying if the name was retrieved successfully
 */
bool rtspClientGetRemoteAddressName(RTSPClient *rtspClient, char *addressName, int maxAddressNameSize);

/*
 * Function: rtspClientCloseConnection
 * Parameters:
 *	rtspClient - already open RTSP client connection (as returned by openRTSPConnection)
 * Returns: a boolean specifying if the connection is closed successfully
 *
 * Remarks:
 * This function will make the RTSP Client pointer NULL, so a closed connection cannot be reused.
 */
bool rtspClientCloseConnection(RTSPClient **rtspClient);

#endif	/* __RTSPCLIENT_H__ */
