/*
 * File: log.c
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "log.h"

#define LOG_BUFFER_SIZE	512

/* Global variable declaration (for direct access in WRITE_LOG macro, see log.h) */
LogLevel defaultLogLevel = LOG_LEVEL_WARNING;

static const char *LOG_COMPONENT_NAME = "log.c";
static const char *LOG_LEVEL_NAMES[] = {
	"FATAL",
	"ERROR",
	"WARNING",
	"INFO",
	"DEBUG"
};
static const char *LOG_MESSAGE_ENDLINE = "\n";
static const int LOG_MESSAGE_ENDLINE_SIZE = 2;			/* Including terminating '\0' */
static const char *LOG_MESSAGE_TOO_BIG_ENDLINE = "...\n";
static const int LOG_MESSAGE_TOO_BIG_ENDLINE_SIZE = 5;		/* Including terminating '\0' */

static FILE *logFile = NULL;
static bool logFileOpenedLocally = false;

static bool logSetFileInternal(FILE *newLogFile);
static bool logWriteInternal(LogLevel logLevel, const char *componentName, const char *logFormat, ...);
static bool logWriteMessage(FILE *privateLogFile, LogLevel logLevel, const char *componentName, const char *logFormat, va_list argumentList);

bool logOpenFile(const char *fileName) {
	FILE *newLogFile = fopen(fileName, "a");
	if(newLogFile == NULL) {
		logWriteInternal(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot open log file \"%s\" for appending. (errno = %d)", fileName, errno);
		return false;
	}
	if(!logSetFileInternal(newLogFile)) {
		return false;
	}
	logFileOpenedLocally = true;
	return true;
}

bool logSetFile(FILE *newLogFile) {
	if(!logSetFileInternal(newLogFile)) {
		return false;
	}
	logFileOpenedLocally = false;
	return true;
}

bool logSetFileInternal(FILE *newLogFile) {
	/* Close any open log-file. */
	if(!logClose()) {
		return false;
	}
	logFile = newLogFile;
	return true;
}

void logSetLogLevel(LogLevel newDefaultlogLevel) {
	defaultLogLevel = newDefaultlogLevel;
}

bool logWrite(LogLevel logLevel, const char *componentName, const char *logFormat, ...) {
	bool result;
	va_list argumentList;

	/* For speed return here if logLevel does not result in actual write operation */
	if(logLevel > defaultLogLevel) {
		return true;
	}

	/* Wrap arguments in va_list structure and call private log-function */
	va_start(argumentList, logFormat);
	result = logWriteMessage(logFile != NULL ? logFile : stderr, logLevel, componentName, logFormat, argumentList);
	va_end(argumentList);
	return result;
}

/* Write log message to stderr when log-system itself fails (eg when new log-file cannot be opened). */
bool logWriteInternal(LogLevel logLevel, const char *componentName, const char *logFormat, ...) {
	bool result;
	va_list argumentList;

	/* For speed return here if logLevel does not result in actual write operation */
	if(logLevel > defaultLogLevel) {
		return true;
	}

	/* Wrap arguments in va_list structure and call private log-function */
	va_start(argumentList, logFormat);
	result = logWriteMessage(stderr, logLevel, componentName, logFormat, argumentList);
	va_end(argumentList);
	return result;
}

/* Write log message. It is assumed that the logLevel is already checked. */
bool logWriteMessage(FILE *privateLogFile, LogLevel logLevel, const char *componentName, const char *logFormat, va_list argumentList) {
	time_t now;
	struct tm timeValue;
	char logBuffer[LOG_BUFFER_SIZE];
	int charsWritten;
	int additionalCharsWritten;

	/* Get the current time */
	time(&now);
	localtime_r(&now, &timeValue);

	/* Construct single buffer with all data and write it (making it 'atomic') */
	charsWritten = snprintf(logBuffer, LOG_BUFFER_SIZE, "%04d-%02d-%02d %02d:%02d:%02d - [%s] - [%s] - ", timeValue.tm_year + 1900, timeValue.tm_mon + 1, timeValue.tm_mday, timeValue.tm_hour, timeValue.tm_min, timeValue.tm_sec, LOG_LEVEL_NAMES[logLevel], componentName != NULL ? componentName : "<unknown>");
	if(charsWritten >= 0 && charsWritten < LOG_BUFFER_SIZE) {
		additionalCharsWritten = vsnprintf(logBuffer + charsWritten, LOG_BUFFER_SIZE - charsWritten, logFormat, argumentList);
		if(additionalCharsWritten < 0) {
			return false;
		}
		/* Add endline string (normally simple newline, but if message was too long replace end of message with '...'. */
		if(additionalCharsWritten + charsWritten + LOG_MESSAGE_ENDLINE_SIZE > LOG_BUFFER_SIZE) {
			memcpy(logBuffer + LOG_BUFFER_SIZE - LOG_MESSAGE_TOO_BIG_ENDLINE_SIZE, LOG_MESSAGE_TOO_BIG_ENDLINE, LOG_MESSAGE_TOO_BIG_ENDLINE_SIZE);
		} else {
			memcpy(logBuffer + charsWritten + additionalCharsWritten, LOG_MESSAGE_ENDLINE, LOG_MESSAGE_ENDLINE_SIZE);
		}
		charsWritten = fputs(logBuffer, privateLogFile);
	}
	return charsWritten > 0;
}

bool logClose() {
	if(logFile != NULL) {

		/* Close any open log-file which was opened using openLogFile. */
		if(logFileOpenedLocally) {
			if(fclose(logFile) != 0) {
				logWriteInternal(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot close logfile. (errno = %d)", errno);
				logFile = NULL;
				return false;
			}
		}
		logFile = NULL;
	}
	return true;
}
