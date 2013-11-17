/*
 * File: log.h
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

#ifndef	__LOG_H__
#define	__LOG_H__

#include <stdio.h>
#include <stdbool.h>

/* Macro for performance. Single (inline) check and prevent function call with (possibly many) arguments. */
/* Use same format as writeLog function, but perform check on log-level inline. */
#define LOG_WRITE(logLevel, ...) (logLevel <= defaultLogLevel ? logWrite(logLevel, __VA_ARGS__) : true)
 
/* Type definition for log-level */
typedef enum {
	LOG_LEVEL_FATAL = 0,
	LOG_LEVEL_ERROR = 1,
	LOG_LEVEL_WARNING = 2,
	LOG_LEVEL_INFO = 3,
	LOG_LEVEL_DEBUG = 4
} LogLevel;

/* Global log-level */
extern LogLevel defaultLogLevel;

/*
 * Function: logOpenFile
 * Parameters:
 *	fileName - path to the log-file to open
 * Returns: a boolean specifying if opening the file is succesful
 *
 * Remarks:
 * Open the log-file. This is a single, application-wide log-file.
 */
bool logOpenFile(const char *logFileName);

/*
 * Function: logSetFile
 * Parameters:
 *	newLogFile - already open/existing log-file to append log messages to
 * Returns: a boolean specifying if setting the log-file is succesful
 *
 * Remarks:
 * Any existing log-file (opened through logOpenFile) is closed before setting the log-file to the newly supplied file.
 */
bool logSetFile(FILE *newLogFile);

/*
 * Function: logSetLogLevel
 * Parameters:
 *	newDefaultLogLevel - log-level used for subsequent calls to writeLog
 */
void logSetLogLevel(LogLevel newDefaultLogLevel);

/*
 * Function: logWrite
 * Parameters:
 *	logLevel - level of the message being logged
 *	componentName - name of the component responsible for writing the log message
 *	logFormat, ... - formatting string and arguments (see fmt parameter of printf and the likes)
 * Returns: a boolean specifying if writing the log message is succesful
 *
 * Remarks:
 * Write log message (part of logFormat parameter) to the log-file. The logFormat is in printf format.
 */
bool logWrite(LogLevel logLevel, const char *componentName, const char *logFormat, ...);

/*
 * Function: logClose
 * Returns: a boolean specifying if closing the log-file is succesful
 *
 * Remarks:
 * Close the log-file (if opened through logOpenFile) and free any resources.
 */
bool logClose();

#endif	/* __LOG_H__ */
