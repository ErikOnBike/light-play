/*
 * File: rtspclient.c
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
#include <stdarg.h>
#include <string.h>
#include "md5/md5.h"
#include "rtspclient.h"
#include "network.h"
#include "log.h"
#include "buffer.h"

/* Define max size of URL ("RTSP://<ip-address>/<session-id>" in which <session-id> is fixed value "1") */
#define MAX_EXTRA_URL_STRING_SIZE	10
#define MAX_URL_STRING_SIZE		(MAX_ADDR_STRING_LENGTH + MAX_EXTRA_URL_STRING_SIZE)

/* Values for buffers and parsing */
#define	MAX_REALM_SIZE			20
#define	MAX_NONCE_SIZE			41
#define	MAX_AUTHENTICATION_BUFFER_SIZE	200
#define	DIGEST_SIZE			16
#define	DIGEST_STRING_SIZE		(DIGEST_SIZE + DIGEST_SIZE)
#define	MAX_NUMBER_STRING_SIZE		11
#define	MAX_HEXNUMBER_STRING_SIZE	9

/* Relevant RTSP Response values */
#define RTSP_RESPONSE_LOW_BANDWIDTH		354
#define RTSP_RESPONSE_NEED_AUTHENTICATION	401

/* Type definition for the RTSP client */
struct RTSPClientStruct {

	/* Network connection and url */
	NetworkConnection *networkConnection;
	char *url;

	/* Request and response buffers */
	RTSPRequest *rtspRequest;
	RTSPResponse *rtspResponse;

	/* Session information */
	uint32_t sessionId;
	uint32_t sequenceNumber;
	bool needAuthentication;
	char realm[MAX_REALM_SIZE];
	uint32_t realmSize;
	char nonce[MAX_NONCE_SIZE];
	uint32_t nonceSize;
};

/* Logging component name */
static const char *LOG_COMPONENT_NAME = "rtspclient.c";

/* Type definition for header field suppliers */
typedef struct {
	RTSPRequestMethod requestMethod;
	bool (*rtspClientHeaderFieldsSupplier)(RTSPClient *rtspClient);
} HeaderFieldsSupplier;

/* Declare internal functions */
static bool rtspClientSendRequest(RTSPClient *rtspClient, RTSPRequestMethod requestMethod, RAOPClient *raopClient, bool (*raopClientContentSupplier)(RAOPClient *raopClient, RTSPRequest *rtspRequest));
static bool rtspClientAddAuthenticationFields(RTSPClient *rtspClient);
static bool rtspClientAddHeaderFields(RTSPClient *rtspClient, RTSPRequestMethod requestMethod);
static bool rtspClientReceiveResponse(RTSPClient *rtspClient);
static bool rtspClientClientGeneralHeaderFieldsSupplier(RTSPClient *rtspClient);
static bool rtspClientOptionsHeaderFieldsSupplier(RTSPClient *rtspClient);
static bool rtspClientAnnounceHeaderFieldsSupplier(RTSPClient *rtspClient);
static bool rtspClientSetupHeaderFieldsSupplier(RTSPClient *rtspClient);
static bool rtspClientRecordHeaderFieldsSupplier(RTSPClient *rtspClient);
static bool rtspClientSetParameterHeaderFieldsSupplier(RTSPClient *rtspClient);
static bool rtspClientFlushHeaderFieldsSupplier(RTSPClient *rtspClient);
static bool rtspClientTeardownHeaderFieldsSupplier(RTSPClient *rtspClient);

/* Table of all header field suppliers */
static const HeaderFieldsSupplier headerFieldsSupplierTable[] = {
	{ RTSP_METHOD_OPTIONS, rtspClientOptionsHeaderFieldsSupplier },
	{ RTSP_METHOD_ANNOUNCE, rtspClientAnnounceHeaderFieldsSupplier },
	{ RTSP_METHOD_SETUP, rtspClientSetupHeaderFieldsSupplier },
	{ RTSP_METHOD_RECORD, rtspClientRecordHeaderFieldsSupplier },
	{ RTSP_METHOD_SET_PARAMETER, rtspClientSetParameterHeaderFieldsSupplier },
	{ RTSP_METHOD_FLUSH, rtspClientFlushHeaderFieldsSupplier },
	{ RTSP_METHOD_TEARDOWN, rtspClientTeardownHeaderFieldsSupplier },
	{ 0, NULL }
};

RTSPClient *rtspClientOpenConnection(const char *hostName, const char *portName) {
	RTSPClient *rtspClient;

	/* Create RTSP client structure */
	if(!bufferAllocate(&rtspClient, sizeof(RTSPClient), "RTSP client")) {
		return NULL;
	}

	/* Open the TCP connection */
	rtspClient->networkConnection = networkOpenConnection(hostName, portName, TCP_CONNECTION, true);
	if(rtspClient->networkConnection == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot open RTSP connection for host \"%s\" on port \"%s\".", hostName, portName);
		rtspClientCloseConnection(&rtspClient);
		return NULL;
	}

	/* Set client URL */
	if(!bufferAllocate(&rtspClient->url, MAX_URL_STRING_SIZE, "URL for RTSP client")) {
		rtspClientCloseConnection(&rtspClient);
		return NULL;
	}
	strcpy(rtspClient->url, "rtsp://");
	if(!networkGetRemoteAddressName(rtspClient->networkConnection, rtspClient->url + strlen(rtspClient->url), MAX_ADDR_STRING_LENGTH)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot open RTSP connection for host \"%s\" on port \"%s\", because local address cannot be retrieved.", hostName, portName);
		rtspClientCloseConnection(&rtspClient);
		return NULL;
	}
	strncat(rtspClient->url, "/1", MAX_URL_STRING_SIZE - strlen(rtspClient->url) - 1);
	/* TODO: check if fixed session id "1" is valid in all cases */

	/* Initialize session information */
	rtspClient->sessionId = 0;
	rtspClient->sequenceNumber = 0;	/* Will be increased before first send */
	rtspClient->needAuthentication = false;
	rtspClient->realmSize = 0;
	rtspClient->nonceSize = 0;

	return rtspClient;
}

bool rtspClientGetLocalAddressName(RTSPClient *rtspClient, char *addressName, int maxAddressNameSize) {
        return networkGetLocalAddressName(rtspClient->networkConnection, addressName, maxAddressNameSize);
}

bool rtspClientGetRemoteAddressName(RTSPClient *rtspClient, char *addressName, int maxAddressNameSize) {
        return networkGetRemoteAddressName(rtspClient->networkConnection, addressName, maxAddressNameSize);
}

bool rtspClientSendCommand(RTSPClient *rtspClient, RTSPRequestMethod requestMethod, RAOPClient *raopClient, bool (*raopClientContentSupplier)(RAOPClient *raopClient, RTSPRequest *rtspRequest)) {
	uint32_t uint32Value;
	int16_t int16Value;

	/* Send request */
	if(!rtspClientSendRequest(rtspClient, requestMethod, raopClient, raopClientContentSupplier)) {
		return false;
	}

	/* Receive response (check returned status code) */
	if(!rtspClientReceiveResponse(rtspClient)) {
		return false;
	}

	/* Repeat request/response if authentication is required */
	if(rtspClient->needAuthentication) {
		
		/* Send request */
		if(!rtspClientSendRequest(rtspClient, requestMethod, raopClient, raopClientContentSupplier)) {
			return false;
		}

		/* Receive response (check returned status code) */
		if(!rtspClientReceiveResponse(rtspClient)) {
			return false;
		}

	}

	/* Check CSeq in response (should always be present) */
	if(!rtspResponseGetSequenceNumber(rtspClient->rtspResponse, &uint32Value)) {
		return false;
	}
	if(uint32Value != rtspClient->sequenceNumber) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "The CSeq value read from RTSP response (%" PRIu32 ") is unequal to send CSeq value (%" PRIu32 ")", uint32Value, rtspClient->sequenceNumber);
	}

	/* Check method specific content */
	if(requestMethod == RTSP_METHOD_SETUP) {

		/* Check Session */
		if(!rtspResponseGetSession(rtspClient->rtspResponse, &uint32Value)) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Response for SETUP command did not provide a valid value for \"Session\"");
			return false;
		}
		rtspClient->sessionId = uint32Value;

		/* Check Transport:server_port */
		if(!rtspResponseGetServerPort(rtspClient->rtspResponse, &int16Value)) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Response for SETUP command did not provide a valid value for \"Transport:server_port\"");
			return false;
		}
		raopClientSetAudioPort(raopClient, int16Value);
	}

	return true;
}

bool rtspClientSendRequest(RTSPClient *rtspClient, RTSPRequestMethod requestMethod, RAOPClient *raopClient, bool (*raopClientContentSupplier)(RAOPClient *raopClient, RTSPRequest *rtspRequest)) {

	/* Create or reset RTSP request */
	if(rtspClient->rtspRequest == NULL) {
		rtspClient->rtspRequest = rtspRequestCreate(requestMethod);
		if(rtspClient->rtspRequest == NULL) {
			return false;
		}
	} else {
		if(!rtspRequestReset(rtspClient->rtspRequest, requestMethod)) {
			return false;
		}
	}

	/* Add header fields to request */
	if(!rtspClientAddHeaderFields(rtspClient, requestMethod)) {
		return false;
	}

	/* Add content to request (if present) */
	if(raopClientContentSupplier != NULL) {
		if(!raopClientContentSupplier(raopClient, rtspClient->rtspRequest)) {
			return false;
		}
	}

	/* Send RTSP request */
	if(!rtspRequestSend(rtspClient->rtspRequest, rtspClient->url, rtspClient->networkConnection)) {
		return false;
	}

	return true;
}

/* TODO: change this into something more readable/maintainable */
bool rtspClientAddAuthenticationFields(RTSPClient *rtspClient) {
	MD5_CTX md5Context;
	uint8_t ha1[DIGEST_SIZE];
	char ha1String[DIGEST_STRING_SIZE];
	uint8_t ha2[DIGEST_SIZE];
	char ha2String[DIGEST_STRING_SIZE];
	uint8_t response[DIGEST_SIZE];
	char responseString[DIGEST_STRING_SIZE];
	char authenticationValue[MAX_AUTHENTICATION_BUFFER_SIZE];
	int index;

	/* Get authentication information (from session or last response) */
	if(rtspClient->realmSize == 0 || rtspClient->nonceSize == 0) {
		if(!rtspResponseGetAuthenticationResponse(rtspClient->rtspResponse, rtspClient->realm, MAX_REALM_SIZE, &rtspClient->realmSize, rtspClient->nonce, MAX_NONCE_SIZE, &rtspClient->nonceSize)) {
			return false;
		}
	}

	/* Perform Digest algorithm: */
	/* HA1 = MD5(username: realm : password); HA2 = MD5(method:digestURI); response = MD5(HA1:nonce:HA2); */ 

	/* Create HA1 */
	MD5_Init(&md5Context);
	MD5_Update(&md5Context, "iTunes", 6);
	MD5_Update(&md5Context, ":", 1);
	MD5_Update(&md5Context, rtspClient->realm, rtspClient->realmSize);
	MD5_Update(&md5Context, ":", 1);
	MD5_Update(&md5Context, "geheim", 6);
	MD5_Final(ha1, &md5Context);
	for(index = 0; index < DIGEST_SIZE; index++) {
		sprintf(&ha1String[index * 2], "%02X", (int)ha1[index]);
	}

	/* Create HA2 */
	MD5_Init(&md5Context);
	MD5_Update(&md5Context, rtspRequestGetMethodName(rtspClient->rtspRequest), strlen(rtspRequestGetMethodName(rtspClient->rtspRequest)));
	MD5_Update(&md5Context, ":", 1);
	MD5_Update(&md5Context, rtspClient->url, strlen(rtspClient->url));
	MD5_Final(ha2, &md5Context);
	for(index = 0; index < DIGEST_SIZE; index++) {
		sprintf(&ha2String[index * 2], "%02X", (int)ha2[index]);
	}

	/* Create response */
	MD5_Init(&md5Context);
	MD5_Update(&md5Context, ha1String, DIGEST_STRING_SIZE);
	MD5_Update(&md5Context, ":", 1);
	MD5_Update(&md5Context, rtspClient->nonce, rtspClient->nonceSize);
	MD5_Update(&md5Context, ":", 1);
	MD5_Update(&md5Context, ha2String, DIGEST_STRING_SIZE);
	MD5_Final(response, &md5Context);
	for(index = 0; index < DIGEST_SIZE; index++) {
		sprintf(&responseString[index * 2], "%02X", (int)response[index]);
	}

	/* Add response */
	snprintf(authenticationValue, MAX_AUTHENTICATION_BUFFER_SIZE, "Digest username=\"iTunes\", realm=\"%.*s\", nonce=\"%.*s\", uri=\"%s\", response=\"%.*s\"",
		rtspClient->realmSize, rtspClient->realm,
		rtspClient->nonceSize, rtspClient->nonce,
		rtspClient->url,
		32, responseString);
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "Authorization", authenticationValue)) {
		return false;
	}

	return true;
}

bool rtspClientAddHeaderFields(RTSPClient *rtspClient, RTSPRequestMethod requestMethod) {
	int index;

	/* Find header fields supplier for current method */
	index = 0;
	while(headerFieldsSupplierTable[index].rtspClientHeaderFieldsSupplier != NULL && headerFieldsSupplierTable[index].requestMethod != requestMethod) {
		index++;
	}
	if(headerFieldsSupplierTable[index].rtspClientHeaderFieldsSupplier == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Unknown method value (%d) when building header for RTSP Request", requestMethod);
		return false;
	}

	/* Add header fields to request */
	if(!headerFieldsSupplierTable[index].rtspClientHeaderFieldsSupplier(rtspClient)) {
		return false;
	}

	/* If authentication is required, add header fields */
	if(rtspClient->needAuthentication || (rtspClient->realmSize > 0 && rtspClient->nonceSize > 0)) {
		rtspClientAddAuthenticationFields(rtspClient);
	}

	return true;
}

bool rtspClientClientGeneralHeaderFieldsSupplier(RTSPClient *rtspClient) {
	char sequenceNumberString[MAX_NUMBER_STRING_SIZE];

	/* Add CSeq header field */
	rtspClient->sequenceNumber++;
	if(snprintf(sequenceNumberString, MAX_NUMBER_STRING_SIZE, "%" PRIu32, rtspClient->sequenceNumber) < 1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot create string for CSeq field");
		return false;
	}
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "CSeq", sequenceNumberString)) {
		return false;
	}

	return true;
}

bool rtspClientOptionsHeaderFieldsSupplier(RTSPClient *rtspClient) {

	/* Add general header fields */
	if(!rtspClientClientGeneralHeaderFieldsSupplier(rtspClient)) {
		return false;
	}

	return true;
}

bool rtspClientAnnounceHeaderFieldsSupplier(RTSPClient *rtspClient) {

	/* Add general header fields */
	if(!rtspClientClientGeneralHeaderFieldsSupplier(rtspClient)) {
		return false;
	}

	return true;
}

bool rtspClientSetupHeaderFieldsSupplier(RTSPClient *rtspClient) {

	/* Add general header fields */
	if(!rtspClientClientGeneralHeaderFieldsSupplier(rtspClient)) {
		return false;
	}

	/* Add SETUP specific header fields */
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "Transport", "RTP/AVP/TCP;unicast;interleaved=0-1;mode=record")) {
		return false;
	}

	return true;
}

bool rtspClientRecordHeaderFieldsSupplier(RTSPClient *rtspClient) {
	char sessionIdString[MAX_HEXNUMBER_STRING_SIZE];

	/* Add general header fields */
	if(!rtspClientClientGeneralHeaderFieldsSupplier(rtspClient)) {
		return false;
	}

	/* Add RECORD specific header fields */
	if(snprintf(sessionIdString, MAX_HEXNUMBER_STRING_SIZE, "%" PRIX32, rtspClient->sessionId) < 1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot create string for Session field");
		return false;
	}
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "Session", sessionIdString)) {
		return false;
	}
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "Range", "npt=0-")) {
		return false;
	}
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "RTP-Info", "seq=0;rtptime=0")) {
		return false;
	}

	return true;
}

bool rtspClientSetParameterHeaderFieldsSupplier(RTSPClient *rtspClient) {

	/* Add general header fields */
	if(!rtspClientClientGeneralHeaderFieldsSupplier(rtspClient)) {
		return false;
	}

	return true;
}

bool rtspClientFlushHeaderFieldsSupplier(RTSPClient *rtspClient) {
	char sessionIdString[MAX_HEXNUMBER_STRING_SIZE];

	/* Add general header fields */
	if(!rtspClientClientGeneralHeaderFieldsSupplier(rtspClient)) {
		return false;
	}

	/* Add FLUSH specific header fields */
	if(snprintf(sessionIdString, MAX_HEXNUMBER_STRING_SIZE, "%" PRIX32, rtspClient->sessionId) < 1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot create string for Session field");
		return false;
	}
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "Session", sessionIdString)) {
		return false;
	}
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "RTP-Info", "seq=0;rtptime=0")) {
		return false;
	}

	return true;
}

bool rtspClientTeardownHeaderFieldsSupplier(RTSPClient *rtspClient) {
	char sessionIdString[MAX_HEXNUMBER_STRING_SIZE];

	/* Add general header fields */
	if(!rtspClientClientGeneralHeaderFieldsSupplier(rtspClient)) {
		return false;
	}

	/* Add TEARDOWN specific header fields */
	if(snprintf(sessionIdString, MAX_HEXNUMBER_STRING_SIZE, "%" PRIX32, rtspClient->sessionId) < 1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot create string for Session field");
		return false;
	}
	if(!rtspRequestAddHeaderField(rtspClient->rtspRequest, "Session", sessionIdString)) {
		return false;
	}

	return true;
}

bool rtspClientReceiveResponse(RTSPClient *rtspClient) {
	int16_t int16Value;

	/* Create RTSP Response if needed */
	if(rtspClient->rtspResponse == NULL) {
		rtspClient->rtspResponse = rtspResponseCreate();
		if(rtspClient->rtspResponse == NULL) {
			return false;
		}
	}

	/* Receive RTSP Response */
	if(!rtspResponseReceive(rtspClient->rtspResponse, rtspClient->networkConnection)) {
		return false;
	}

	/* Check return code */
	if(!rtspResponseGetStatus(rtspClient->rtspResponse, &int16Value)) {
		return false;
	}
	rtspClient->needAuthentication = false;
	if(int16Value != 200) {
		if(int16Value > 200 && int16Value < 300) {
			logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "RTSP Response received return code %" SCNi16 ". This is a 'success' response, but might indicate a warning on the server.", int16Value);
		} else {
			if(int16Value != RTSP_RESPONSE_NEED_AUTHENTICATION) {
				logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "RTSP Response failed with code %" SCNi16 "%s.", int16Value, int16Value == RTSP_RESPONSE_LOW_BANDWIDTH ? " AirTunes device is probably playing audio already." : "");
				return false;
			}
			rtspClient->needAuthentication = true;
		}
	}

	return true;
}

bool rtspClientCloseConnection(RTSPClient **rtspClient) {
	bool result;

	/* Close all opened/allocated resource. Continu if a failure occurs, but remember failure for final result. */
	result = true;
	if(*rtspClient != NULL) {
		if((*rtspClient)->networkConnection != NULL) {
			if(!networkCloseConnection(&(*rtspClient)->networkConnection)) {
				result = false;
			}
		}
		if((*rtspClient)->rtspRequest != NULL) {
			if(!rtspRequestFree(&(*rtspClient)->rtspRequest)) {
				result = false;
			}
		}
		if((*rtspClient)->rtspResponse != NULL) {
			if(!rtspResponseFree(&(*rtspClient)->rtspResponse)) {
				result = false;
			}
		}
		if(!bufferFree(&(*rtspClient)->url)) {
			result = false;
		}
		if(!bufferFree(rtspClient)) {
			result = false;
		}
	}

	return result;
}
