#include <time.h>
#include <inttypes.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "raopclient.h"
#include "log.h"
#include "buffer.h"

static const char *LOG_COMPONENT_NAME = "light-play.c";

/* Local variable */
static RAOPClient *raopClient = NULL;

/* Internal functions */
static void printUsage(const char *appName, const char *printFormat, ...);
static void signalHandler(int signalNumber);


int main(int argc, char** argv) {
	M4AFile *m4aFile;
	char *url;
	char *password;
	char *portName;
	char *fileName;
	LogLevel logLevel;
	char *logFileName;
	struct timespec playingOffset;
	char *ptr;
	int i;

	/* Initialize */
	logSetLogLevel(LOG_LEVEL_WARNING);
	logSetFile(stderr);
	url = NULL;
	password = NULL;
	portName = "5000";
	fileName = NULL;
	logLevel = LOG_LEVEL_WARNING;
	logFileName = NULL;
	playingOffset.tv_sec = 0;
	playingOffset.tv_nsec = 0;

	/* Parse command line arguments */
	i = 1;
	while(i < argc) {
		/* Parse options */
		if(argv[i][0] == '-' && argv[i][1] != '-') {
			switch(argv[i][1]) {
				case '?':
				case 'h':
					/* Help on usage */
					printUsage(argv[0], NULL);
				return 1;
				case 'c':
					/* Set password */
					if(argv[i][2] == '\0') {
						if(i + 1 < argc) {
							i++;
							password = argv[i];
						} else {
							printUsage(argv[0], "Parameter value for 'c' not specified.");
							return 1;
						}
					} else {
						password = &argv[i][2];
					}
				break;
				case 'p':
					/* Set port (default: 5000) */
					if(argv[i][2] == '\0') {
						if(i + 1 < argc) {
							i++;
							portName = argv[i];
						} else {
							printUsage(argv[0], "Parameter value for 'p' not specified.");
							return 1;
						}
					} else {
						portName = &argv[i][2];
					}
				break;
				case 'v':
					/* Set verbosity of logging */
					switch(argv[i][2]) {
						case 'e':
							logLevel = LOG_LEVEL_ERROR;
						break;
						case 'w':
						case '\0':
							logLevel = LOG_LEVEL_WARNING;
						break;
						case 'i':
							logLevel = LOG_LEVEL_INFO;
						break;
						case 'd':
							logLevel = LOG_LEVEL_DEBUG;
						break;
						default:
							printUsage(argv[0], "Additional unsupported character(s) '%s' after option 'v'.", &argv[i][2]);
						return 1;
					}

					/* Check for superfluous characters */
					if(argv[i][2] != '\0' && argv[i][3] != '\0') {
						printUsage(argv[0], "Additional character(s) '%s' after option 'v%c'.", &argv[i][3], argv[i][2]);
						return 1;
					}
				break;
				case 'l':
					/* Set logging to file */
					if(argv[i][2] == '\0') {
						if(i + 1 < argc) {
							i++;
							logFileName = argv[i];
						} else {
							printUsage(argv[0], "Parameter value for 'l' not specified.");
							return 1;
						}
					} else {
						logFileName = &argv[i][2];
					}
				break;
				case 'o':
					/* Set offset in file from where to start playing */
					if(argv[i][2] == '\0') {
						if(i + 1 < argc) {
							i++;
							playingOffset.tv_sec = (time_t)strtoul(argv[i], &ptr, 10);
						} else {
							printUsage(argv[0], "Parameter value for 'o' not specified.");
							return 1;
						}
					} else {
						playingOffset.tv_sec = (time_t)strtoul(&argv[i][2], &ptr, 10);
					}
					if(*ptr != '\0') {
						printUsage(argv[0], "Additional character(s) '%s' after offset value 'o'.", ptr);
						return 1;
					}
				break;
				default:
					printUsage(argv[0], "Unknown parameter '%s' specified.", argv[i]);
				return 1;
			}
		} else {
			/* If argument starts with a '-' it must be a filename */
			if(argv[i][0] == '-') {
				if(url == NULL) {
					printUsage(argv[0], "Unknown parameter specified '%s'.", argv[i]);
					return 1;
				}
				fileName = &argv[i][1];	/* A filename starting with a '-' */
			} else {
				if(url == NULL) {
					url = argv[i];
				} else if(fileName == NULL) {
					fileName = argv[i];
				} else {
					printUsage(argv[0], "Too many parameters specified (first unknown '%s').", argv[i]);
					return 1;
				}
			}
		}
		i++;
	}

	/* Check parameters */
	if(url == NULL) {
		if(fileName == NULL) {
			printUsage(argv[0], "Required parameters <url> and <filename> not specified.");
		} else {
			printUsage(argv[0], "Required parameter <filename> not specified.");
		}
		return 1;
	}

	/* Set logging level and file */
	logSetLogLevel(logLevel);
	if(logFileName != NULL) {
		logOpenFile(logFileName);
	}

	/* Set signal handler */
	if(signal(SIGINT, signalHandler) == SIG_ERR) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Cannot set signal handler for SIGINT (continuing)");
	}

	/* Describe what is passed as argument */
	logWrite(LOG_LEVEL_INFO, LOG_COMPONENT_NAME, "Going to play file '%s' on url '%s:%s'", fileName, url, portName);

	/* Open M4AFile */
	m4aFile = m4aFileOpen(fileName);
	if(m4aFile == NULL) {
		return 1;
	}
	if(!m4aFileParse(m4aFile)) {
		return 1;
	}

	/* Open RAOP client */
	raopClient = raopClientOpenConnection(url, portName, password);
	if(raopClient == NULL) {
		m4aFileClose(&m4aFile);
		return 1;
	}

	/* Play M4AFile */
	if(!raopClientPlayM4AFile(raopClient, m4aFile, &playingOffset)) {
		raopClientCloseConnection(&raopClient);
		m4aFileClose(&m4aFile);
		return 1;
	}

	/* Wait for file to finish playing */
	raopClientWait(raopClient);

	/* Close RAOP client and M4AFile */
	raopClientCloseConnection(&raopClient);
	if(!m4aFileClose(&m4aFile)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Failed to close m4aFile");
		return 1;
	}

	/* Check if all open buffers are closed */
	if(bufferGetBuffersInUse() != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "A number of allocated buffers (%" PRIi32 ") is not freed properly", bufferGetBuffersInUse());
		return 1;
	}

	/* Close log file (if needed) */
	if(logFileName != NULL) {
		logClose();
	}

	return 0;
}

void printUsage(const char *appName, const char *printFormat, ...) {
	char *shortAppName;
	va_list argumentList;

	/* Take short name of application (only works for systems with '/' as path separator) */
	shortAppName = strrchr(appName, '/');
	if(shortAppName == NULL) {
		shortAppName = (char *)appName;
	} else {
		shortAppName++;	/* Skip '/' itself */
		if(shortAppName == '\0') {
			shortAppName = (char *)appName;
		}
	}

	/* Print usage */
	fprintf(stderr, "Usage: %s [-?hpvqlo] <url> <filename>\n\n" \
			"    -? | -h          Print this usage message\n" \
			"    -c[ ]<password>  Set password for using AirPort express\n" \
			"    -p[ ]<portname>  Set name/number of AirTunes port (default: 5000)\n"
			"    -v[e|w|i|d]      Set logging verbosity (default: w)\n"
			"                         e: only errors\n"
			"                         w: errors and warnings (default)\n"
			"                         i: errors, warnings and info\n"
                        "                         d: all (includes debug info)\n"
			"    -l[ ]<filename>  Set logging to specified file\n"
			"    -o[ ]<offset>    Set offset (in seconds) from begin of file where to start playing\n", shortAppName);

	/* Print additional message if present */
	if(printFormat != NULL) {
		/* Wrap arguments in va_list structure and call printf-function */
		va_start(argumentList, printFormat);
		fputs("\n\n", stderr);
		vfprintf(stderr, printFormat, argumentList);
		va_end(argumentList);
		fputs("\n", stderr);
	}
}

void signalHandler(int signalNumber) {
	struct timespec progress;

	if(signalNumber == SIGINT) {

		/* Check how far playing has come */
		if(raopClientGetProgress(raopClient, &progress)) {
			logWrite(LOG_LEVEL_INFO, LOG_COMPONENT_NAME, "Progress so far: %" PRIu32 " seconds", (uint32_t)(progress.tv_sec));
		}

		/* Stop playing audio */
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Stop playing before end of file on user request."); 
		raopClientStopPlaying(raopClient);
	}
}
