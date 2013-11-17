/*
 * File: raopclient.c
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

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include "raopclient.h"
#include "rtspclient.h"
#include "rtsprequest.h"
#include "network.h"
#include "log.h"
#include "buffer.h"
#include "utils.h"

/* Values for volume. Anything below VOLUME_MIN_VALUE will be set to 'muted'. Anything above VOLUME_MAX_VALUE will be set to VOLUME_MAX_VALUE. */
#define VOLUME_DEFAULT			15.0
#define VOLUME_MUTED			0.0
#define VOLUME_MIN_VALUE		0.01
#define VOLUME_MAX_VALUE		30.0

/* Values for buffers and parsing */
#define VOLUME_INTERNAL_OFFSET		-30.0
#define VOLUME_INTERNAL_MUTED		-144.0
#define	UNUSED_PORT_NUMBER		0
#define	PLAYING_TIME_LAG_SECONDS	2
#define	PLAYING_TIME_LAG_NANO_SECONDS	0
#define MAX_ANNOUNCE_CONTENT_SIZE_NO_ADDRESSES	160
#define MAX_ANNOUNCE_CONTENT_SIZE	(MAX_ANNOUNCE_CONTENT_SIZE_NO_ADDRESSES + MAX_ADDR_STRING_LENGTH + MAX_ADDR_STRING_LENGTH)
#define	MAX_SET_PARAMETER_CONTENT_SIZE	20
#define	MAX_NUMBER_STRING_SIZE		11
#define AUDIO_MESSAGE_HEADER_SIZE	16

/* Type definition for the RAOP client */
struct RAOPClientStruct {

	/* Connections and port info for communicating with server */
        char *hostName;
	RTSPClient *rtspClient;
	NetworkConnection *audioConnection;
	int audioPort;

	/* Thread for handling audio packets */
	pthread_t audioThread;
	bool audioThreadJoinable;	/* TODO: Not implemented thread-safe, but worst case is error message that joining failed */

	/* Session information */
	float volume;
	M4AFile *m4aFile;
	bool isSendingAudio;
	struct timespec playingTimeOffset;	/* Absolute offset when playing started (takes lag into account) */
	struct timespec startTime;		/* Start time within file */
};

static const char *LOG_COMPONENT_NAME = "raopclient.c";

/* Lag between sending first audio packet and real audio being transmitted through the AirTunes-device onto an audio-device */
static const struct timespec PLAYING_TIME_LAG = {
	.tv_sec = PLAYING_TIME_LAG_SECONDS,
	.tv_nsec = PLAYING_TIME_LAG_NANO_SECONDS
};

/* Declare internal functions */
static bool raopClientInitialize(RAOPClient *raopClient);
static bool raopClientStartPlaying(RAOPClient *raopClient);
static void *raopClientSendAudio(void *arg);
static bool raopClientSendAudioMessages(RAOPClient *raopClient);
static bool raopClientWaitForBufferedAudio(RAOPClient *raopClient);
static bool raopClientSetupAudioConnection(RAOPClient *raopClient);
static bool raopClientAnnounceContentSupplier(RAOPClient *raopClient, RTSPRequest *rtspRequest);
static bool raopClientSetVolumeContentSupplier(RAOPClient *raopClient, RTSPRequest *rtspRequest);
static bool raopClientCloseConnectionInternal(RAOPClient **raopClient);

RAOPClient *raopClientOpenConnection(const char *hostName, const char *portName) {
	RAOPClient *raopClient;

	/* Create raop client structure */
	if(!bufferAllocate(&raopClient, sizeof(RAOPClient), "RAOP client")) {
		return NULL;
	}

	/* Initialize structure */
	if(!raopClientInitialize(raopClient)) {
		bufferFree(&raopClient);
		return NULL;
	}
	raopClient->hostName = strdup((char *)hostName);
	if(raopClient->hostName == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot allocate memory (%lu bytes) for hostname in new RAOP Client to host.", (unsigned long)strlen(hostName));
		bufferFree(&raopClient);
		return NULL;
	}
	raopClient->volume = VOLUME_DEFAULT;	/* Set volume separately (not in 'raopClientInitialize'), so it retains it value between different calls to 'raopClientPlayM4AFile'. */

	/* Open the RTSP connection */
	raopClient->rtspClient = rtspClientOpenConnection(hostName, portName);
	if(raopClient->rtspClient == NULL) {
		raopClientCloseConnection(&raopClient);
		return NULL;
	}

	return raopClient;
}

bool raopClientInitialize(RAOPClient *raopClient) {
	/*
		Do not set volume here, since this function is called before playing new files.
		It is set separately so it will retain its value between files.
	*/
	raopClient->hostName = NULL;
	raopClient->rtspClient = NULL;
	raopClient->audioConnection = NULL;
	raopClient->audioPort = UNUSED_PORT_NUMBER;
	raopClient->audioThreadJoinable = false;
	raopClient->m4aFile = NULL;
	raopClient->isSendingAudio = false;
	timespecInitialize(&raopClient->playingTimeOffset);
	timespecInitialize(&raopClient->startTime);

	return true;
}

bool raopClientSetAudioPort(RAOPClient *raopClient, int16_t audioPort) {
	raopClient->audioPort = audioPort;
	
	return true;
}

bool raopClientPlayM4AFile(RAOPClient *raopClient, M4AFile *m4aFile, struct timespec *startTime) {

	/* Initialize audio configuration */
	raopClient->m4aFile = m4aFile;
	if(startTime != NULL) {
		timespecCopy(&raopClient->startTime, startTime);
	}

	/* Send OPTIONS command to initialize RTSP connection (will fail if AirTunes device requires authentication) */
	if(!rtspClientSendCommand(raopClient->rtspClient, RTSP_METHOD_OPTIONS, raopClient, NULL)) {
		return false;
	}

	/* Send ANNOUNCE command */
	if(!rtspClientSendCommand(raopClient->rtspClient, RTSP_METHOD_ANNOUNCE, raopClient, raopClientAnnounceContentSupplier)) {
		return false;
	}

	/* Send SETUP command */
	if(!rtspClientSendCommand(raopClient->rtspClient, RTSP_METHOD_SETUP, raopClient, NULL)) {
		return false;
	}

	/* Setup audio connection */
	if(!raopClientSetupAudioConnection(raopClient)) {
		return false;
	}

	/* Send RECORD command */
	if(!rtspClientSendCommand(raopClient->rtspClient, RTSP_METHOD_RECORD, raopClient, NULL)) {
		return false;
	}

	/* Send SET_PARAMETER command (for the volume) */
	if(!rtspClientSendCommand(raopClient->rtspClient, RTSP_METHOD_SET_PARAMETER, raopClient, raopClientSetVolumeContentSupplier)) {
		return false;
	}

	/* Send audio data (in separate thread) */
	if(!raopClientStartPlaying(raopClient)) {
		return false;
	}

	return true;
}

bool raopClientStartPlaying(RAOPClient *raopClient) {

	/* Set status of client */
	raopClient->isSendingAudio = true;

	/* Start a new thread for sending audio packets */
	if(pthread_create(&raopClient->audioThread, NULL, raopClientSendAudio, raopClient) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot create thread for sending audio packets through audio port");
		raopClient->isSendingAudio = false;
		return false;
	}
	raopClient->audioThreadJoinable = true;

	return true;
}

void *raopClientSendAudio(void *arg) {
	RAOPClient *raopClient;

	/* Initialize */
	raopClient = (RAOPClient *)arg;

	/* Position at starting sample, according to 'startTime' */
	if(!m4aFileSetSampleOffset(raopClient->m4aFile, &raopClient->startTime)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot set initial offset for playing file");
		raopClient->audioThreadJoinable = false;
		pthread_exit(NULL);
		return NULL;
	}

	/* Keep absolute time offset */
	if(clock_gettime(CLOCK_MONOTONIC, &raopClient->playingTimeOffset) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot retrieve clock value at start of playing (errno = %d)", errno);
		raopClient->audioThreadJoinable = false;
		pthread_exit(NULL);
		return NULL;
	}

	/* Already calculate lag time into offset value */
	timespecAdd(&raopClient->playingTimeOffset, &PLAYING_TIME_LAG);

	/* Send audio messages */
	if(!raopClientSendAudioMessages(raopClient)) {
		raopClient->audioThreadJoinable = false;
		pthread_exit(NULL);
		return NULL;
	}

	/* Wait for buffered audio messages to be played */
	if(!raopClientWaitForBufferedAudio(raopClient)) {
		raopClient->audioThreadJoinable = false;
		pthread_exit(NULL);
		return NULL;
	}

	/* Playing is done, stop the thread */
	raopClient->audioThreadJoinable = false;
	pthread_exit(NULL);
	return NULL;
}

bool raopClientSendAudioMessages(RAOPClient *raopClient) {
	uint8_t *audioMessage;
	uint32_t sampleSize;
	uint16_t packetLength;

	/* Create buffer for audio message, large enough to contain the largest sample */
	if(!bufferAllocate(&audioMessage, AUDIO_MESSAGE_HEADER_SIZE + m4aFileGetLargestSampleSize(raopClient->m4aFile), "audio sample buffer")) {
		return false;
	}

	/* Write info to log */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Start to send audio packets.");

	/* As long as playing is not stopped and data is available send audio packets */
	while(m4aFileHasMoreSamples(raopClient->m4aFile) && raopClient->isSendingAudio) {

		/* Get audio sample (copy into audio message buffer) */
		if(!m4aFileGetNextSample(raopClient->m4aFile, audioMessage + AUDIO_MESSAGE_HEADER_SIZE, &sampleSize)) {
			bufferFree(&audioMessage);
			return false;
		}

		/* Set header of audio message */
		memset(audioMessage, 0, 16);
		audioMessage[0] = 0x24;
		audioMessage[4] = 0xf0;
		audioMessage[5] = 0xff;
		packetLength = (uint16_t)htons(sampleSize + 12);
		memcpy(audioMessage + 2, &packetLength, sizeof(uint16_t));

		/* Send message */
		if(!networkSendMessage(raopClient->audioConnection, audioMessage, AUDIO_MESSAGE_HEADER_SIZE + sampleSize)) {
			bufferFree(&audioMessage);
			return false;
		}
	}

	/* Free buffer */
	bufferFree(&audioMessage);

	return true;
}

bool raopClientWaitForBufferedAudio(RAOPClient *raopClient) {
	struct timespec progress;
	struct timespec length;
	uint32_t remainingSeconds;

	/* Get progress (how much is played already) */
	if(!raopClientGetProgress(raopClient, &progress)) {
		return false;
	}

	/* Get length (how much there is to play) */
	if(!m4aFileGetLength(raopClient->m4aFile, &length)) {
		return false;
	}

	/* If audio is still buffered (length >= progress), wait for total playing time to pass */
	if(length.tv_sec >= progress.tv_sec) {
		remainingSeconds = length.tv_sec - progress.tv_sec + 1;	/* Add 1 second for remaining partial second */
		while(raopClient->isSendingAudio && remainingSeconds > 0) {
			if(sleep(1) != 0) {
				logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Waiting for buffered data to being played is interrupted.");
				remainingSeconds = 0;
			} else {
				remainingSeconds--;
			}
		}
	}

	return true;
}

bool raopClientSetVolume(RAOPClient *raopClient, float volume) {

	/* Validate input */
	if(volume < VOLUME_MIN_VALUE) {
		volume = VOLUME_MUTED;
	}
	if(volume > VOLUME_MAX_VALUE) {
		volume = VOLUME_MAX_VALUE;
	}
	raopClient->volume = volume;

	/* If already playing, send new volume value */
	if(raopClient->isSendingAudio) {
		if(!rtspClientSendCommand(raopClient->rtspClient, RTSP_METHOD_SET_PARAMETER, raopClient, raopClientSetVolumeContentSupplier)) {
			return false;
		}
	}

	return true;
}

bool raopClientGetProgress(RAOPClient *raopClient, struct timespec *progress) {
	struct timespec currentTime;

	/* Get current time */
	if(clock_gettime(CLOCK_MONOTONIC, &currentTime) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot retrieve clock value when calculating progress (errno = %d)", errno);
		return false;
	}

	/* Calculate progress made */
	timespecSubtract(&currentTime, &raopClient->playingTimeOffset, progress);

	/* Add starttime in case we didn't start at the beginning of the file */
	timespecAdd(progress, &raopClient->startTime);

	return true;
}

bool raopClientStopPlaying(RAOPClient *raopClient) {
	bool result;

	/* Return if nothing to stop */
	if(!raopClient->isSendingAudio) {
		return true;
	}

	/* Stop sending and streaming audio */
	raopClient->isSendingAudio = false;
	result = true;

	/* Wait for audio thread to stop */
	if(raopClient->audioThreadJoinable) {
		raopClient->audioThreadJoinable = false;
		if(pthread_join(raopClient->audioThread, NULL) != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot join audio thread (to wait for it to stop). Flush audio anyway.");
			result = false;
		}
	}

	/* Send FLUSH command (stop AirTunes from streaming/playing its buffered content) */
	if(!rtspClientSendCommand(raopClient->rtspClient, RTSP_METHOD_FLUSH, raopClient, NULL)) {
		result = false;
	}

	/* Send TEARDOWN command */
	if(!rtspClientSendCommand(raopClient->rtspClient, RTSP_METHOD_TEARDOWN, raopClient, NULL)) {
		result = false;
	}

	return result;
}

bool raopClientWait(RAOPClient *raopClient) {

	/* Only wait if an audio thread is created, otherwise nothing to wait for */
	if(raopClient->audioThreadJoinable) {
		raopClient->audioThreadJoinable = false;
		if(pthread_join(raopClient->audioThread, NULL) != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot join audio thread to wait for it to stop");
			return false;
		}

		/* In case the playing has finished normally, audio is not sent anymore */
		raopClient->isSendingAudio = false;
	}

	return true;
}

bool raopClientSetupAudioConnection(RAOPClient *raopClient) {
	char portNumberString[MAX_NUMBER_STRING_SIZE];

	/* Open TCP connection to server for audio */
	if(snprintf(portNumberString, MAX_NUMBER_STRING_SIZE, "%" PRIu32, raopClient->audioPort) < 1) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot create string for server port number");
		return false;
	}
	raopClient->audioConnection = networkOpenConnection(raopClient->hostName, portNumberString, TCP_CONNECTION, true);
	if(raopClient->audioConnection == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot open audio connection to server [%s] on port [%s]", raopClient->hostName, portNumberString);
		return false;
	}

	return true;
}

bool raopClientAnnounceContentSupplier(RAOPClient *raopClient, RTSPRequest *rtspRequest) {
	char content[MAX_ANNOUNCE_CONTENT_SIZE];
	size_t contentSize;
	char localAddressName[MAX_ADDR_STRING_LENGTH];
	char remoteAddressName[MAX_ADDR_STRING_LENGTH];

	/* Add ANNOUNCE specific content */
	if(!rtspClientGetLocalAddressName(raopClient->rtspClient, localAddressName, MAX_ADDR_STRING_LENGTH)) {
		return false;
	}
	if(!rtspClientGetRemoteAddressName(raopClient->rtspClient, remoteAddressName, MAX_ADDR_STRING_LENGTH)) {
		return false;
	}
	if(snprintf(content, MAX_ANNOUNCE_CONTENT_SIZE,
			"v=0\r\n"
			"o=iTunes 1 O IN IP4 %s\r\n"
			"s=iTunes\r\n"
			"c=IN IP4 %s\r\n"
			"t=0 0\r\n"
			"m=audio 0 RTP/AVP 96\r\n"
			"a=rtpmap:96 AppleLossless\r\n"
			"a=fmtp:96 4096 0 16 40 10 14 2 255 0 0 %" PRIu32 "\r\n", localAddressName, remoteAddressName, m4aFileGetTimescale(raopClient->m4aFile)) < 0) {
		return false;
	}
	contentSize = strlen(content);

	if(!rtspRequestSetContent(rtspRequest, (uint8_t *)content, contentSize, "application/sdp")) {
		return false;
	}

	return true;
}

bool raopClientSetVolumeContentSupplier(RAOPClient *raopClient, RTSPRequest *rtspRequest) {
	char content[MAX_SET_PARAMETER_CONTENT_SIZE];
	size_t contentSize;

	/* Add SET_PARAMETER specific content */
	if(snprintf(content, MAX_SET_PARAMETER_CONTENT_SIZE,
			"volume: %.1f\r\n", raopClient->volume >= VOLUME_MIN_VALUE ? VOLUME_INTERNAL_OFFSET + raopClient->volume : VOLUME_INTERNAL_MUTED) < 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot create string for volume parameter");
		return false;
	}
	contentSize = strlen(content);

	if(!rtspRequestSetContent(rtspRequest, (uint8_t *)content, contentSize, "text/parameters")) {
		return false;
	}

	return true;
}

bool raopClientCloseConnection(RAOPClient **raopClient) {
	bool result;

	/* Close all opened/allocated resource. Continu if a failure occurs, but remember failure for final result. */
	result = true;
	if(!raopClientCloseConnectionInternal(raopClient)) {
		result = false;
	}
	if(!bufferFree(raopClient)) {
		result = false;
	}

	return result;
}

bool raopClientCloseConnectionInternal(RAOPClient **raopClient) {
	bool result;

	/* Answer true if raopClient already NULL */
	if(*raopClient == NULL) {
		return true;
	}

	/* Close all opened/allocated resource. Continu if a failure occurs, but remember failure for final result. */
	result = true;
	if((*raopClient)->audioThreadJoinable) {
		if(pthread_cancel((*raopClient)->audioThread) != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot cancel audio thread of RAOP client");
			result = false;
		}
		(*raopClient)->audioThreadJoinable = false;
	}
	if((*raopClient)->audioConnection != NULL) {
		if(!networkCloseConnection(&(*raopClient)->audioConnection)) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot close server connection of RAOP client");
			result = false;
		}
	}
	if((*raopClient)->rtspClient != NULL) {
		if(!rtspClientCloseConnection(&(*raopClient)->rtspClient)) {
			result = false;
		}
	}
	(*raopClient)->m4aFile = NULL;	/* Is opened elsewhere, let it be closed there as well */
	free((*raopClient)->hostName);	/* hostName is allocated using strdup, do not use bufferFree here */

	if(!result) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Not all RAOP client resources have been properly closed and freed, this might influence the application stability");
	}

	return result;
}
