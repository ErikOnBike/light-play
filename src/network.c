/*
 * File: network.c
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

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include "network.h"
#include "log.h"
#include "buffer.h"

#define UNUSED_SOCKET_DESCRIPTOR	-1

/* Type definition for the network connection */
struct NetworkConnectionStruct {
	int socketDescriptor;
	NetworkConnectionType connectionType;
	bool isClient;
	struct sockaddr *localAddress;
	socklen_t localAddressSize;
	struct sockaddr *remoteAddress;
	socklen_t remoteAddressSize;
};

/* Logging component name */
static const char *LOG_COMPONENT_NAME = "network.c";

static bool networkGetAddressInfo(const char *hostName, const char *portName, NetworkConnectionType connectionType, struct addrinfo **addressInfo);
static bool networkCopyAddressInfo(NetworkConnection *networkConnection, const char *hostName, const char *portName);
static bool networkCopySocketAddress(struct sockaddr **destinationAddress, socklen_t *destinationAddressSize, struct sockaddr *sourceAddress, socklen_t sourceAddressSize);
static bool networkGetAddressName(struct sockaddr *address, char *addressName, int maxAddressNameSize);
static bool networkReceiveMessageInternal(NetworkConnection *networkConnection, uint8_t *messageBuffer, size_t maxMessageSize, size_t *messageSize, int flags);
static bool networkCloseSocket(NetworkConnection *networkConnection);

NetworkConnection *networkOpenConnection(const char *hostName, const char *portName, NetworkConnectionType connectionType, bool makeClient) {
	NetworkConnection *networkConnection;
	struct addrinfo* addressInfoResult;
	struct addrinfo* addressInfo;
	int connectResult;

	/* Create network connection structure */
	if(!bufferAllocate(&networkConnection, sizeof(NetworkConnection), "network connection")) {
		return NULL;
	}
	networkConnection->socketDescriptor = UNUSED_SOCKET_DESCRIPTOR;
	networkConnection->connectionType = connectionType;
	networkConnection->isClient = false;
	networkConnection->localAddress = NULL;
	networkConnection->localAddressSize = 0;
	networkConnection->remoteAddress = NULL;
	networkConnection->remoteAddressSize = 0;

	/* Get address info for creating socket (for server-mode do not supply a hostname) */
	if(!networkGetAddressInfo(makeClient ? hostName : NULL, portName, connectionType, &addressInfoResult)) {
		networkCloseConnection(&networkConnection);
		return NULL;
	}

	/* Open the socket connection and if successful connect or bind to it */
	/* This will try the different addresses in the linked list part of the result addressInfoResult */
	addressInfo = addressInfoResult;
	while(addressInfo != NULL && networkConnection->socketDescriptor == UNUSED_SOCKET_DESCRIPTOR) {
		networkConnection->socketDescriptor = socket(addressInfo->ai_family, addressInfo->ai_socktype, addressInfo->ai_protocol);
		if(networkConnection->socketDescriptor != -1) {
			if(makeClient) {
				connectResult = connect(networkConnection->socketDescriptor, addressInfo->ai_addr, addressInfo->ai_addrlen);
			} else {
				connectResult = bind(networkConnection->socketDescriptor, addressInfo->ai_addr, addressInfo->ai_addrlen);
			}
			if(connectResult == 0) {
				/* Keep address information of local and remote side */
				if(networkCopyAddressInfo(networkConnection, hostName, portName)) {
					networkConnection->isClient = makeClient;
				} else {
					networkCloseSocket(networkConnection);
					addressInfo = addressInfo->ai_next;
				}
			} else {
				logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot %s network connection to host \"%s\" on the opened socket%s. (errno = %d)", makeClient ? "connect" : "bind", hostName, addressInfo->ai_next != NULL ? " (going to try another address)" : "", errno);
				networkCloseSocket(networkConnection);
				addressInfo = addressInfo->ai_next;
			}
		} else {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot open a socket for the network connection%s. (errno = %d)", addressInfo->ai_next != NULL ? " (going to try another address)" : "", errno);
			networkConnection->socketDescriptor = UNUSED_SOCKET_DESCRIPTOR;
			addressInfo = addressInfo->ai_next;
		}
	}

	/* In case no connection was made, free networkConnection and thereby answering a NULL value at the end of the function */
	if(addressInfo == NULL) {
		networkCloseConnection(&networkConnection);
	}

	/* Free the adress info structure */
	freeaddrinfo(addressInfoResult);

	/* Write info to log */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Opened network %s connection to host %s on port %s", connectionType == UDP_CONNECTION ? "UDP" : (connectionType == TCP_CONNECTION ? "TCP" : "(unknown type)"), hostName, portName);

	return networkConnection;
}

NetworkConnectionType networkGetConnectionType(NetworkConnection *networkConnection) {
	return networkConnection->connectionType;
}

bool networkGetAddressInfo(const char *hostName, const char *portName, NetworkConnectionType connectionType, struct addrinfo **addressInfo) {
	struct addrinfo hints;

	/* Setup address info and hinst structure to get the IPv4/v6 address */
	memset(&hints, 0, sizeof(struct addrinfo));
	if(connectionType == UDP_CONNECTION) {
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;
		hints.ai_flags = AI_PASSIVE;
	} else if(connectionType == TCP_CONNECTION) {
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = 0;
	} else {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Unsupported connection type. Unclear how to setup a connection. Failing.");
		return false;
	}
	hints.ai_family = AF_UNSPEC;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	if(getaddrinfo(hostName, portName, &hints, addressInfo) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot get address information for host \"%s\" with port \"%s\".", hostName == NULL ? "<any>" : hostName, portName);
		return false;
	}

	return true;
}

bool networkCopyAddressInfo(NetworkConnection *networkConnection, const char *hostName, const char *portName) {
	struct sockaddr localAddress;
	socklen_t localAddressSize;
	struct addrinfo* remoteAddressInfo;
	bool result;

	/* Retrieve local address info (from socket) */
	localAddressSize = sizeof(struct sockaddr);
	if(getsockname(networkConnection->socketDescriptor, &localAddress, &localAddressSize) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot retrieve local address from socket (errno = %d)", errno);
		return false;
	}

	/* Copy local address info */
	if(!networkCopySocketAddress(&networkConnection->localAddress, &networkConnection->localAddressSize, &localAddress, localAddressSize)) {
		return false;
	}

	/* Retrieve remote address info (simply using UDP as connectionType) */
	if(!networkGetAddressInfo(hostName, portName, UDP_CONNECTION, &remoteAddressInfo)) {
		return false;
	}

	/* Copy remote address info (if found and usable) */
	result = true;
	if(remoteAddressInfo->ai_family == AF_INET || remoteAddressInfo->ai_family == AF_INET6) {
		if(!networkCopySocketAddress(&networkConnection->remoteAddress, &networkConnection->remoteAddressSize, remoteAddressInfo->ai_addr, remoteAddressInfo->ai_addrlen)) { 
			result = false;
		}
	} else {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Unknown ai_family %d found when retrieving remote address.", remoteAddressInfo->ai_family);
		result = false;
	}

	/* Free the adress info structure */
	freeaddrinfo(remoteAddressInfo);

	return result;
}

bool networkCopySocketAddress(struct sockaddr **destinationAddress, socklen_t *destinationAddressSize, struct sockaddr *sourceAddress, socklen_t sourceAddressSize) {
	*destinationAddressSize = sourceAddressSize;
	if(!bufferAllocate(destinationAddress, sourceAddressSize, "socket address")) {
		return false;
	}
	memcpy(*destinationAddress, sourceAddress, sourceAddressSize);
	return true;
}

bool networkGetLocalAddressName(NetworkConnection *networkConnection, char *addressName, int maxAddressNameSize) {
	return networkGetAddressName(networkConnection->localAddress, addressName, maxAddressNameSize);
}

bool networkGetRemoteAddressName(NetworkConnection *networkConnection, char *addressName, int maxAddressNameSize) {
	return networkGetAddressName(networkConnection->remoteAddress, addressName, maxAddressNameSize);
}

bool networkGetAddressName(struct sockaddr *address, char *addressName, int maxAddressNameSize) {
	if(address->sa_family == AF_INET) {
		if(inet_ntop(AF_INET, &((struct sockaddr_in *)address)->sin_addr, addressName, (socklen_t)maxAddressNameSize) == NULL) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot retrieve IPv4 address through inet_ntop (errno = %d)", errno);
			return false;
		} 
	} else if(address->sa_family == AF_INET6) {
		if(inet_ntop(AF_INET6, &((struct sockaddr_in6 *)address)->sin6_addr, addressName, (socklen_t)maxAddressNameSize) == NULL) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot retrieve IPv6 address through inet_ntop (errno = %d)", errno);
			return false;
		} 
	} else {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Unknown sa_family %d found when retrieving IP address", address->sa_family);
		return false;
	}

	return true;
}

bool networkSendMessage(NetworkConnection *networkConnection, uint8_t *messageBuffer, size_t messageSize) {
	ssize_t result;
 
	if(networkConnection == NULL) {
		return false;
	}
	if(networkConnection->isClient) {
		result = send(networkConnection->socketDescriptor, messageBuffer, messageSize, 0);
	} else {
		result = sendto(networkConnection->socketDescriptor, messageBuffer, messageSize, 0, networkConnection->remoteAddress, networkConnection->remoteAddressSize);
	}
	if(result == -1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot send network message to server. (errno = %d)", errno);
		return false;
	} else if(result != messageSize) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot send full network message to server (ie partial send). (errno = %d)", errno);
		return false;
	}

	return true;
}

bool networkReceiveMessage(NetworkConnection *networkConnection, uint8_t *messageBuffer, size_t maxMessageSize, size_t *messageSize) {
	return networkReceiveMessageInternal(networkConnection, messageBuffer, maxMessageSize, messageSize, 0);
}

bool networkReceiveMessageInternal(NetworkConnection *networkConnection, uint8_t *messageBuffer, size_t maxMessageSize, size_t *messageSize, int flags) {
	struct sockaddr remoteAddress;
	socklen_t remoteAddressSize;
	ssize_t result;

	if(networkConnection == NULL) {
		return false;
	}
	if(networkConnection->isClient) {
		result = recv(networkConnection->socketDescriptor, messageBuffer, maxMessageSize, flags);
	} else {
		remoteAddressSize = sizeof(struct sockaddr);
		memset(&remoteAddress, 0, remoteAddressSize);
		result = recvfrom(networkConnection->socketDescriptor, messageBuffer, maxMessageSize, flags, &remoteAddress, &remoteAddressSize);
		/* TODO: check value remoteAddress */
	}
	if(result == -1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot receive network message from host. (errno = %d)", errno);
		return false;
	} else if(result == 0) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Cannot receive network message from host (host may already have closed the connection).");
		/* In some cases this might be an acceptable situation, let the caller handle it. Just accept empty message. */
	}
	*messageSize = result;

	return true;
}

bool networkIsMessageAvailable(NetworkConnection *networkConnection) {
	uint8_t byte;
	size_t size;

	/* Peek for a single byte to decide if a message is available */ 
	return networkReceiveMessageInternal(networkConnection, &byte, 1, &size, MSG_PEEK) && size == 1;
}

bool networkCloseConnection(NetworkConnection **networkConnection) {
	bool result;

	/* Close all opened/allocated resource. Continu if a failure occurs, but remember failure for final result. */
	result = true;
	if(*networkConnection != NULL) {
		if(!networkCloseSocket(*networkConnection)) {
			result = false;
		}
		if(!bufferFree(&(*networkConnection)->localAddress)) {
			result = false;
		}
		if(!bufferFree(&(*networkConnection)->remoteAddress)) {
			result = false;
		}
		if(!bufferFree(networkConnection)) {
			result = false;
		}
	}
	return result;
}

bool networkCloseSocket(NetworkConnection *networkConnection) {
	bool result;

	/* Close all opened resources. Continu if a failure occurs, but remember failure for final result. */
	result = true;
	if(networkConnection->socketDescriptor != UNUSED_SOCKET_DESCRIPTOR) {
		if(networkConnection->isClient) {
			if(shutdown(networkConnection->socketDescriptor, SHUT_RDWR) != 0) {
				/* Just ignore an already closed connection (ignore ENOTCONN) */
				if(errno != ENOTCONN) {
					logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot shutdown the network connection with the server. (errno = %d)", errno);
					result = false;
				}
			}
		}
		if(close(networkConnection->socketDescriptor) != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot close the socket connection with the server. (errno = %d)", errno);
			result = false;
		}
		networkConnection->socketDescriptor = UNUSED_SOCKET_DESCRIPTOR;
	}

	return result;
}
