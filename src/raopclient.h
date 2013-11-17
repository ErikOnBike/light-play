/*
 * File: raopclient.h
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

#ifndef	__RAOPCLIENT_H__
#define	__RAOPCLIENT_H__

#include <time.h>
#include "m4afile.h"

/* Type definition for RAOPClient */
typedef struct RAOPClientStruct RAOPClient;

/*
 * Function: raopClientOpenConnection
 * Parameters:
 *	hostName - name of host
 *	portName - name of port
 *      password - password (optional)
 * Returns: RAOP Client structure
 *
 * Remarks:
 * The portName parameter can either be the string representation of a port number but can also be a service name like "raop" or "rtsp" (if your system supports this).
 */
RAOPClient *raopClientOpenConnection(const char *hostName, const char *portName, const char *password);

/*
 * Function: raopClientSetAudioPort
 * Parameters:
 *	raopClient - already open RAOP Client (as returned by raopClientOpenConnection)
 *	audioPort - the port (number) for sending audio data
 * Returns: a boolean specifying if the audio port is set successfully
 */
bool raopClientSetAudioPort(RAOPClient *raopClient, int16_t audioPort);

/*
 * Function: raopClientPlayM4AFile
 * Parameters:
 *	raopClient - already open RAOP Client (as returned by raopClientOpenConnection)
 *	m4aFile - M4AFile to play
 *	startTime - time within file from which playing starts (offset from beginning of file)
 * Returns: a boolean specifying if the client could start playing successfully
 *
 * Remarks:
 * The RAOP Client will play the file asynchronously (ie in separate thread). With raopClientWait a thread can wait for the
 * playing to finish (either through normal termination when the complete file has been played or by stopping it by calling
 * raopClientStopPlaying).
 */
bool raopClientPlayM4AFile(RAOPClient *raopClient, M4AFile *m4aFile, struct timespec *startTime);

/*
 * Function: raopClientSetVolume
 * Parameters:
 *	raopClient - already open RAOP Client (as returned by raopClientOpenConnection)
 *	volume - volume as range from minimum -30.0 to maximum 0.0 or -144.0 for muted (Apple AirTunes volume range)
 * Returns: a boolean specifying if the client could set the volume successfully
 *
 * Remarks:
 * The volume range is taken from the (unofficial) Apple AirTunes specification (see http://nto.github.io/AirPlay.html).
 * If a file is playing the (audible) volume will change. If no file is playing yet, the volume is set as the default value for when
 * a new file is being played.
 */
bool raopClientSetVolume(RAOPClient *raopClient, float volume);

/*
 * Function: raopClientGetProgress
 * Parameters:
 *	raopClient - already open RAOP Client (as returned by raopClientOpenConnection)
 *	progress - timespec structure to contain the current progress (from the beginning of the file)
 * Returns: a boolean specifying if the client could retrieve the progress successfully
 *
 * Remarks:
 * @@When fails? What if nothing plays?
 */
bool raopClientGetProgress(RAOPClient *raopClient, struct timespec *progress);

/*
 * Function: raopClientStopPlaying
 * Parameters:
 *	raopClient - already open RAOP Client (as returned by raopClientOpenConnection)
 * Returns: a boolean specifying if the client could stop playing successfully
 *
 * Remarks:
 * The RAOP Client plays files asynchronously (ie in separate thread). Also buffering can take place at the receiver. Therefore
 * stopping the playing might take a number of milliseconds to complete (ie it might not become silent instantly). The current
 * progress (as retrieved through raopClientGetProgress) will however indicate as if the playing stopped immediately.
 * If no playing has been started raopClientStopPlaying will just return 'true'.
 */
bool raopClientStopPlaying(RAOPClient *raopClient);

/*
 * Function: raopClientWait
 * Parameters:
 *	raopClient - already open RAOP Client (as returned by raopClientOpenConnection)
 * Returns: a boolean specifying if the client could wait successfully
 *
 * Remarks:
 * The RAOP Client plays files asynchronously (ie in separate thread). Calling raopClientWait lets the current thread wait for the playing
 * to complete. The playing will complete when the full file has been played or when the playing is stopped by calling raopClientStopPlaying.
 * If no playing has been started raopClientWait will just return 'true'.
 */
bool raopClientWait(RAOPClient *raopClient);

/*
 * Function: raopClientCloseConnection
 * Parameters:
 *	raopClient - already open RAOP Client (as returned by raopClientOpenConnection)
 * Returns: a boolean specifying if the connection is closed successfully
 *
 * Remarks:
 * This function will make the RAOP Client pointer NULL, so a closed connection cannot be reused.
 */
bool raopClientCloseConnection(RAOPClient **raopClient);

#endif	/* __RAOPCLIENT_H__ */
