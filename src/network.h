/*
 * File: network.h
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

#ifndef	__NETWORK_H__
#define	__NETWORK_H__

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

/* Type definition for NetworkConnection */
typedef struct NetworkConnectionStruct NetworkConnection;

/* Type definition for connection types */
typedef enum {
	TCP_CONNECTION = 0,
	UDP_CONNECTION = 1
} NetworkConnectionType;

/* Maximum name of IP address (IPv4 and IPv6 supported) */
#ifdef INET6_ADDRSTRLEN
#define MAX_ADDR_STRING_LENGTH (INET6_ADDRSTRLEN)
#else
#define MAX_ADDR_STRING_LENGTH 46
#endif

/*
 * Function: networkOpenConnection
 * Parameters:
 *	hostName - name of host
 *	portName - name of port
 *	connectionType - type (TCP/UDP) of connection
 *	doConnect - connect to the host (turning connection into client)
 * Returns: Network connection structure
 *
 * Remarks:
 * The portName parameter can either be the string representation of a port number but can also be a service name like "ftp" or "telnet".
 */
NetworkConnection *networkOpenConnection(const char *hostName, const char *portName, NetworkConnectionType connectionType, bool doConnect);

/*
 * Function: networkGetConnectionType
 * Parameters:
 *	networkConnection - already open network connection (as returned by networkOpenConnection)
 * Returns: the network-connection-type of the network-connection
 */
NetworkConnectionType networkGetConnectionType(NetworkConnection *networkConnection);

/*
 * Function: networkGetLocalAddressName
 * Parameters:
 *	networkConnection - already open network connection (as returned by networkOpenConnection)
 *	addressName - array of characters where name of local address will be stored (local IP address)
 *	maxAddressNameSize - size of the name buffer (as number of characters including the position for '\0')
 * Returns: a boolean specifying if the name was retrieved successfully
 */
bool networkGetLocalAddressName(NetworkConnection *networkConnection, char *addressName, int maxAddressNameSize);

/*
 * Function: networkGetRemoteAddressName
 * Parameters:
 *	networkConnection - already open network connection (as returned by networkOpenConnection)
 *	addressName - array of characters where name of remote address will be stored (remote IP address)
 *	maxAddressNameSize - size of the name buffer (as number of characters including the position for '\0')
 * Returns: a boolean specifying if the name was retrieved successfully
 */
bool networkGetRemoteAddressName(NetworkConnection *networkConnection, char *addressName, int maxAddressNameSize);

/*
 * Function: networkSendMessage
 * Parameters:
 *	networkConnection - already open network connection (as returned by networkOpenConnection)
 *	messageBuffer - array of bytes containing the message data
 *	messageSize - size of the message data (as number of bytes)
 * Returns: a boolean specifying if the message was sent successfully
 */
bool networkSendMessage(NetworkConnection *networkConnection, uint8_t *messageBuffer, size_t messageSize);

/*
 * Function: networkReceiveMessage
 * Parameters:
 *	networkConnection - already open network connection (as returned by networkOpenConnection)
 *	messageBuffer - array of bytes to put the message data in
 *	maxMessageSize - size of the message buffer (as number of bytes): the maximum number of bytes to receive
 *	messageSize - actual size of the message received (as number of bytes)
 * Returns: a boolean specifying if the message was received successfully
 *
 * Remarks:
 * The parameter maxMessageSize should denote how many bytes the message buffer can handle. Since the network connection
 * can be used for any kind of data, no '\0' byte termination is done for character based messages.
 */
bool networkReceiveMessage(NetworkConnection *networkConnection, uint8_t *messageBuffer, size_t maxMessageSize, size_t *messageSize);

/*
 * Function: networkIsMessageAvailable
 * Parameters:
 *	networkConnection - already open network connection (as returned by networkOpenConnection)
 * Returns: a boolean specifying if a message is available to receive using networkReceiveMessage
 */
bool networkIsMessageAvailable(NetworkConnection *networkConnection);

/*
 * Function: networkCloseConnection
 * Parameters:
 *	networkConnection - already open network connection (as returned by networkOpenConnection)
 * Returns: a boolean specifying if the connection was closed successfully
 *
 * Remarks:
 * This function will make the Network Connection pointer NULL, so a closed connection cannot be reused.
 */
bool networkCloseConnection(NetworkConnection **networkConnection);

#endif	/* __NETWORK_H__ */
