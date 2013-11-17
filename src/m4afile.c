/*
 * File: m4afile.c
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
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include "log.h"
#include "buffer.h"
#include "m4afile.h"

/* Constants */
#define	UNUSED_OFFSET			0xffffffff
#define	DEFAULT_FRAMES_PER_PACKET	4096
#define	ONE_SECOND_IN_NANO_SECONDS	1000000000L

/* Some macros for handling long integer string values in MP4 format and a printf macro in an 'inttypes.h' style. */
/* The compiler can optimise some of the byte shuffling. The printing through PRIls32 (ls = long string) is done by */
/* printing 4 separate characters. This is none-optimal performance wise, but should only occur in case something */
/* fails. Performance is probably not of major importance then. The byte ordering of the host makes printing the */
/* 'long string' non-trivial if done from within a single printf (or logWrite) statement. Therefore this solution */
/* is chosen. */
#define ASCII_TO_INT32(char1, char2, char3, char4) ((int32_t)(char1 << 24) | (int32_t)(char2 << 16) | (int32_t)(char3 << 8) | (int32_t)char4)
#define INT32_TO_ASCII(intValue) (char)((intValue >> 24) & 0xff), (char)((intValue >> 16) & 0xff), (char)((intValue >> 8) & 0xff), (char)(intValue & 0xff)
#define PRIls32	"c%c%c%c"
#define	NO_BOXTYPE		ASCII_TO_INT32('<', 'n', 'o', '>')
#define APPLE_FILE_TYPE		ASCII_TO_INT32('M', '4', 'A', ' ')
#define	ALAC_ENCODING_TYPE	ASCII_TO_INT32('a', 'l', 'a', 'c')
#define AAC_ENCODING_TYPE	ASCII_TO_INT32('m', 'p', '4', 'a')
#define	METADATA_DATA_TYPE	ASCII_TO_INT32('d', 'a', 't', 'a')
#define	METADATA_NAME_TYPE	ASCII_TO_INT32('n', 'a', 'm', 'e')
#define	METADATA_MEAN_TYPE	ASCII_TO_INT32('m', 'e', 'a', 'n')
#define	ITUNES_ANNOTATION_TYPE	ASCII_TO_INT32('-', '-', '-', '-')

/* Logging component name */
static const char *LOG_COMPONENT_NAME = "m4afile.c";


/* Type definition for the M4AFile status */
typedef enum {
	M4AFILE_OK = 0,
	M4AFILE_ERROR = -1,
	M4AFILE_PARSED_WITH_WARNINGS = 1
} M4AFileStatus;

/* Type definition for structure storing M4A-file information needed during parsing and handling */
struct M4AFileStruct {
	FILE *dataStream;		/* Stream for reading data (mdat box content) */
	FILE *sizeStream;		/* Stream for reading table with samples sizes (stsz box content) */
	uint32_t dataOffset;		/* Offset of data (mdat box content) in bytes from begin of file */
	uint32_t sizeOffset;		/* Offset of table with samples sizes (stsz box content) in bytes from begin of file */
	uint32_t totalSize;		/* Total size (in bytes) of file */
	uint32_t samplesCount;		/* Number of samples */
	uint32_t totalSampleSize;	/* Total size (in bytes) of all samples */
	uint32_t largestSampleSize;	/* Size (in bytes) of the largest sample */
	uint32_t timescale;		/* As number of samples per second */
	uint32_t duration;		/* In timescale units */
	M4AFileEncoding encoding;	/* Encoding format of the data */
	M4AFileStatus status;		/* Status (set during parsing) */
	
	/* Handler for processing metadata (called during parsing) */
	void (*metadataHandler)(uint32_t boxType, uint8_t *buffer, uint32_t bufferSize, M4AFileMetadataType metadataType);
};

/* Type definition for MP4 box parsers */
typedef struct {
	uint32_t type;
	uint32_t (*boxParser)(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
} MP4BoxParser;

/* Type definition for MP4 box generic parser functions */
typedef uint32_t (*mp4BoxParserGeneric)(M4AFile *m4aFile, uint32_t boxType);

/* Internal functions related to parsing MP4 boxes */
/* All mp4BoxParse<xyz> functions return the number of bytes read and/or skipped during parsing. */
/* Header parsing of MP4 boxes is done in generic functions (mp4BoxParse and mp4BoxParseAppleData) */
/* so all parser functions get as parameter boxType (the type of box being parsed) and boxBytesLeft */
/* (the number of bytes remaining within the box). */
static uint32_t mp4BoxParse(M4AFile *m4aFile, uint32_t containerBoxType);
static uint32_t mp4BoxParseFileType(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseMediaHeader(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseTrackHeader(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseSampleDescriptions(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseSampleDescription(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseSampleTimes(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseSampleSizes(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseMetadata(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseAppleAnnotation(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseAppleData(M4AFile *m4aFile, uint32_t annotationBoxType);
static uint32_t mp4BoxParseMediaData(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxSkip(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseContainer(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft);
static uint32_t mp4BoxParseContainerInternal(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft, mp4BoxParserGeneric boxParser);

/* Table of all audio-related MP4 boxes within an M4A file */
static const MP4BoxParser mp4BoxParserTable[] = {
	{ ASCII_TO_INT32('f', 't', 'y', 'p'), mp4BoxParseFileType },		/* File type */
	{ ASCII_TO_INT32('m', 'o', 'o', 'v'), mp4BoxParseContainer },		/* Movie/presentation (ie also audio) */
	{ ASCII_TO_INT32('m', 'v', 'h', 'd'), mp4BoxParseMediaHeader },		/* Movie header */
	{ ASCII_TO_INT32('t', 'r', 'a', 'k'), mp4BoxParseContainer },		/* Track */
	{ ASCII_TO_INT32('t', 'k', 'h', 'd'), mp4BoxParseTrackHeader },		/* Track header */
	{ ASCII_TO_INT32('u', 'd', 't', 'a'), mp4BoxParseContainer },		/* User data */
	{ ASCII_TO_INT32('m', 'd', 'i', 'a'), mp4BoxParseContainer },		/* Media */
	{ ASCII_TO_INT32('m', 'd', 'h', 'd'), mp4BoxParseMediaHeader },		/* Media header */
	{ ASCII_TO_INT32('h', 'd', 'l', 'r'), mp4BoxSkip },			/* Media handler reference (unused) */
	{ ASCII_TO_INT32('m', 'i', 'n', 'f'), mp4BoxParseContainer },		/* Media info */
	{ ASCII_TO_INT32('s', 'm', 'h', 'd'), mp4BoxSkip },			/* Sound media header (unused) */
	{ ASCII_TO_INT32('d', 'i', 'n', 'f'), mp4BoxParseContainer },		/* Data info */
	{ ASCII_TO_INT32('d', 'r', 'e', 'f'), mp4BoxSkip },			/* Data reference (unused) */
	{ ASCII_TO_INT32('s', 't', 'b', 'l'), mp4BoxParseContainer },		/* Sample table */
	{ ASCII_TO_INT32('s', 't', 's', 'd'), mp4BoxParseSampleDescriptions },	/* Sample descriptions (container) */
	{ ASCII_TO_INT32('a', 'l', 'a', 'c'), mp4BoxParseSampleDescription },	/* Sample description (single instance) */
	{ ASCII_TO_INT32('m', 'p', '4', 'a'), mp4BoxParseSampleDescription },	/* Sample description (single instance) */
	{ ASCII_TO_INT32('s', 't', 't', 's'), mp4BoxParseSampleTimes },		/* Sample times */
	{ ASCII_TO_INT32('s', 't', 's', 'c'), mp4BoxSkip },			/* Sample to chunk mapping (unused) */
	{ ASCII_TO_INT32('s', 't', 's', 'z'), mp4BoxParseSampleSizes },		/* Sample sizes */
	{ ASCII_TO_INT32('s', 't', 'c', 'o'), mp4BoxSkip },			/* Chunk offset table (unused) */
	{ ASCII_TO_INT32('m', 'e', 't', 'a'), mp4BoxParseMetadata },		/* Metadata */
	{ ASCII_TO_INT32('i', 'l', 's', 't'), mp4BoxParseContainer },		/* Apple's item list */
	{ ITUNES_ANNOTATION_TYPE,             mp4BoxParseAppleAnnotation },	/* Apple's annotation (iTunes specific) */
	{ ASCII_TO_INT32('f', 'r', 'e', 'e'), mp4BoxSkip },			/* Free box (for easier updating) */
	{ ASCII_TO_INT32('m', 'd', 'a', 't'), mp4BoxParseMediaData },		/* Media data */

	/* The following MP4 boxes are (all optional) metadata boxes specific to iTunes */
	/* The information is gathered from the website: http://code.google.com/p/mp4v2/wiki/iTunesMetadata */
	/* and research on existing M4A collections. */
	/* Only the (seemingly) relevant audio elements are parsed. This means that a metadata handler will receive */
	/* the content from these boxes. All other (movie/tv related) elements which are commented out will not be */
	/* parsed and are skipped by the general parsing mechanism. If the logging level is set appropriately, the */
	/* presence of such a box (or any other unknown box) in a M4A file will be logged as a warning. Not parsing */
	/* a box might leave certain information unavailable for the calling code. In audio files the movie/tv */
	/* related elements should not be present, so no warnings should result for M4A (ie audio) files. */
	{ ASCII_TO_INT32(0xa9, 'n', 'a', 'm'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Name) */
	{ ASCII_TO_INT32(0xa9, 'A', 'R', 'T'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Artist) */
	{ ASCII_TO_INT32('a', 'A', 'R', 'T'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Album artist) */
	{ ASCII_TO_INT32(0xa9, 'a', 'l', 'b'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Album) */
	{ ASCII_TO_INT32(0xa9, 'g', 'r', 'p'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Grouping) */
	{ ASCII_TO_INT32(0xa9, 'w', 'r', 't'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Composer/writer) */
	{ ASCII_TO_INT32(0xa9, 'c', 'm', 't'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Comment) */
	{ ASCII_TO_INT32('g', 'n', 'r', 'e'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Genre) */
	{ ASCII_TO_INT32(0xa9, 'g', 'e', 'n'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Genre, user defined) */
	{ ASCII_TO_INT32(0xa9, 'd', 'a', 'y'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Release date) */
	{ ASCII_TO_INT32('t', 'r', 'k', 'n'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Track number) */
	{ ASCII_TO_INT32('d', 'i', 's', 'k'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Disc number) */
	{ ASCII_TO_INT32('t', 'm', 'p', 'o'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Tempo) */
	{ ASCII_TO_INT32('c', 'p', 'i', 'l'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Compilation) */
/*	{ ASCII_TO_INT32('t', 'v', 's', 'h'), mp4BoxParseAppleAnnotation }, */	/* Apple's annotation (TV Show name) */
/*	{ ASCII_TO_INT32('t', 'v', 'e', 'n'), mp4BoxParseAppleAnnotation }, */	/* Apple's annotation (TV Episode ID) */
/*	{ ASCII_TO_INT32('t', 'v', 's', 'n'), mp4BoxParseAppleAnnotation }, */	/* Apple's annotation (TV Season) */
/*	{ ASCII_TO_INT32('t', 'v', 'e', 's'), mp4BoxParseAppleAnnotation }, */	/* Apple's annotation (TV Episode) */
/*	{ ASCII_TO_INT32('t', 'v', 'n', 'n'), mp4BoxParseAppleAnnotation }, */	/* Apple's annotation (TV Network) */
	{ ASCII_TO_INT32('d', 'e', 's', 'c'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Description) */
	{ ASCII_TO_INT32('l', 'd', 'e', 's'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Long description) */
	{ ASCII_TO_INT32(0xa9, 'l', 'y', 'r'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Lyrics) */
	{ ASCII_TO_INT32('s', 'o', 'n', 'm'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Sort name) */
	{ ASCII_TO_INT32('s', 'o', 'a', 'r'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Sort artist) */
	{ ASCII_TO_INT32('s', 'o', 'a', 'a'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Sort album artist) */
	{ ASCII_TO_INT32('s', 'o', 'a', 'l'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Sort album) */
	{ ASCII_TO_INT32('s', 'o', 'c', 'o'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Sort composer) */
	{ ASCII_TO_INT32('s', 'o', 's', 'n'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Sort show) */
	{ ASCII_TO_INT32('c', 'o', 'v', 'r'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Cover art) */
	{ ASCII_TO_INT32('c', 'p', 'r', 't'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Copyright) */
	{ ASCII_TO_INT32(0xa9, 't', 'o', 'o'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Encoding tool) */
	{ ASCII_TO_INT32(0xa9, 'e', 'n', 'c'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Encoded by) */
	{ ASCII_TO_INT32('p', 'u', 'r', 'd'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Purchase date) */
	{ ASCII_TO_INT32('p', 'c', 's', 't'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Podcast) */
	{ ASCII_TO_INT32('p', 'u', 'r', 'l'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Podcast URL) */
	{ ASCII_TO_INT32('k', 'e', 'y', 'w'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Keywords) */
	{ ASCII_TO_INT32('c', 'a', 't', 'g'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Category) */
/*	{ ASCII_TO_INT32('h', 'd', 'v', 'd'), mp4BoxParseAppleAnnotation }, */	/* Apple's annotation (HD Video) */
	{ ASCII_TO_INT32('s', 't', 'i', 'k'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Media type) */
	{ ASCII_TO_INT32('r', 't', 'n', 'g'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Content rating) */
	{ ASCII_TO_INT32('p', 'g', 'a', 'p'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Gapless playback) */
	{ ASCII_TO_INT32('a', 'p', 'I', 'D'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Purchase account) */
	{ ASCII_TO_INT32('a', 'k', 'I', 'D'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Account type) */
	{ ASCII_TO_INT32('c', 'n', 'I', 'D'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Unknown) */
	{ ASCII_TO_INT32('s', 'f', 'I', 'D'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Country code) */
	{ ASCII_TO_INT32('a', 't', 'I', 'D'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Unknown) */
	{ ASCII_TO_INT32('p', 'l', 'I', 'D'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Unknown) */
	{ ASCII_TO_INT32('g', 'e', 'I', 'D'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Unknown) */
	{ ASCII_TO_INT32(0xa9, 's', 't', '3'), mp4BoxParseAppleAnnotation },	/* Apple's annotation (Unknown) */
	{ 0, NULL }
};

/* Declare internal functions */
static void m4aFileInitialize(M4AFile *m4aFile);
static void m4aFileSetTimescale(M4AFile *m4aFile, uint32_t timescale);
static void m4aFileSetDuration(M4AFile *m4aFile, uint32_t duration);
static void m4aFileSetTimeValue(M4AFile *m4aFile, uint32_t timeValue, uint32_t *timeField, const char *timeFieldName);
static bool m4aFileSetDataOffset(M4AFile *m4aFile);
static bool m4aFileSetSizeOffset(M4AFile *m4aFile);
static bool m4aFileSetOffsetValue(M4AFile *m4aFile, uint32_t *offsetField, const char *offsetFieldName);
static bool m4aFileCheckVersionAndFlags(M4AFile *m4aFile, uint32_t boxType, uint8_t *boxVersion, uint8_t expectedVersion, uint32_t *boxFlags, uint32_t expectedBitsOn, uint32_t expectedBitsOff);
static bool m4aFileReadDuration(M4AFile *m4aFile, uint32_t boxType, uint8_t boxVersion, uint32_t *duration);
static bool m4aFileReadMetadataContent(M4AFile *m4aFile, uint32_t annotationBoxType, uint32_t boxType, uint32_t metadataType, uint32_t dataSize);
static bool m4aFileSkipBytes(M4AFile *m4aFile, uint32_t boxType, uint32_t byteCount);
static bool m4aFileReadUnsignedLong(M4AFile *m4aFile, uint32_t boxType, uint32_t *result);
static bool m4aFileReadData(M4AFile *m4aFile, uint8_t *data, uint32_t dataSize);
static uint32_t read4ByteUnsignedInt32(FILE *stream);
static bool didReadErrorOccur(FILE *stream);

/* Public functions */
M4AFile *m4aFileOpen(const char *fileName) {
	M4AFile *m4aFile;

	/* Create M4AFile structure */
	if(!bufferAllocate(&m4aFile, sizeof(M4AFile), "M4A file")) {
		return NULL;
	}
	m4aFileInitialize(m4aFile);

	/* Open the file and assign to dataStream (is arbitrary choice, could have been sizeStream as well). */
	/* Is only temporary until the file is parsed complete. After parsing dataStream and sizeStream are set */
	/* to their specific position within the M4A file. (mdat-box and stsz-box) */
	m4aFile->dataStream = fopen(fileName, "rb");
	if(m4aFile->dataStream == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot open file \"%s\". (errno = %d)", fileName, errno);
		m4aFileClose(&m4aFile);
		return NULL;
	}

	/* Store the file size */
	if(fseek(m4aFile->dataStream, 0, SEEK_END) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot seek the end of the file for \"%s\". (errno = %d)", fileName, errno);
		m4aFileClose(&m4aFile);
		return NULL;
	}
	m4aFile->totalSize = ftell(m4aFile->dataStream);

	/* Start at the begin of the file again */
	if(fseek(m4aFile->dataStream, 0, SEEK_SET) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot seek the begin of the file for \"%s\". (errno = %d)", fileName, errno);
		m4aFileClose(&m4aFile);
		return NULL;
	}

	/* Open size stream. It can only be used after parsing the file. */
	m4aFile->sizeStream = fopen(fileName, "rb");
	if(m4aFile->sizeStream == NULL) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot open file \"%s\". (errno = %d)", fileName, errno);
		m4aFileClose(&m4aFile);
		return NULL;
	}

	return m4aFile;
}

bool m4aFileSetMetadataHandler(M4AFile *m4aFile, M4AFileMetadataHandler metadataHandler) {
	if(m4aFile->metadataHandler != NULL) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "A metadatahandler for M4AFile is already set. The new handler replaces the old.");
	}
	m4aFile->metadataHandler = metadataHandler;

	return true;
}

bool m4aFileParse(M4AFile *m4aFile) {
	while(m4aFile->status != M4AFILE_ERROR && mp4BoxParse(m4aFile, NO_BOXTYPE) > 0) {
		/* Repeatedly read boxes. Error handling is done inside mp4BoxParse(). */
	}

	/* Set streams to their appropriate location */
	if(m4aFile->status != M4AFILE_ERROR) {
		if(fseek(m4aFile->dataStream, m4aFile->dataOffset, SEEK_SET) != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot seek the begin of the data stream. (errno = %d)", errno);
		}
		if(fseek(m4aFile->sizeStream, m4aFile->sizeOffset, SEEK_SET) != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot seek the begin of the size stream. (errno = %d)", errno);
		}
	}

	return m4aFile->status != M4AFILE_ERROR;	/* Return true on warnings */
}

bool m4aFileHasParsedWithWarnings(M4AFile *m4aFile) {
	return m4aFile->status == M4AFILE_PARSED_WITH_WARNINGS;
}

M4AFileEncoding m4aFileGetEncoding(M4AFile *m4aFile) {
	return m4aFile->encoding;
}

bool m4aFileGetLength(M4AFile *m4aFile, struct timespec *time) {

	/* Calculate time */
	time->tv_sec = m4aFile->duration / m4aFile->timescale;
	time->tv_nsec = ONE_SECOND_IN_NANO_SECONDS * (m4aFile->duration - time->tv_sec * m4aFile->timescale) / m4aFile->timescale;

	return true;
}

uint32_t m4aFileGetTimescale(M4AFile *m4aFile) {
	return m4aFile->timescale;
}

uint32_t m4aFileGetSamplesCount(M4AFile *m4aFile) {
	return m4aFile->samplesCount;
}

uint32_t m4aFileGetLargestSampleSize(M4AFile *m4aFile) {
	return m4aFile->largestSampleSize;
}

bool m4aFileSetSampleOffset(M4AFile *m4aFile, struct timespec *timeOffset) {
	uint32_t sampleOffset;
	uint32_t sampleSize;

	/* Calculate at which sample to start */
	sampleOffset = m4aFile->timescale * timeOffset->tv_sec / DEFAULT_FRAMES_PER_PACKET;
	if(sampleOffset >= m4aFile->samplesCount) {
		return false;
	}

	/* Start at first sample */
	if(fseek(m4aFile->sizeStream, m4aFile->sizeOffset, SEEK_SET) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot set sample offset value % for size stream (errno = %d)" PRIu32, sampleOffset, errno);
		return false;
	}
	if(fseek(m4aFile->dataStream, m4aFile->dataOffset, SEEK_SET) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot set sample offset value % for data stream (errno = %d)" PRIu32, sampleOffset, errno);
		return false;
	}

	/* Iterate samples until at correct position */
	while(sampleOffset > 0) {
		sampleSize = read4ByteUnsignedInt32(m4aFile->sizeStream);
		if(didReadErrorOccur(m4aFile->sizeStream)) {
			return false;
		}
		if(fseek(m4aFile->dataStream, sampleSize, SEEK_CUR) != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot skip to next sample when trying to set offset (errno = %d)", errno);
			return false;
		}
		sampleOffset--;
	}

	return true;
}

uint32_t m4aFileGetCurrentSampleIndex(M4AFile *m4aFile) {
	uint32_t offset;

	/* Calculate position in size offset to decide which sample is current */
	offset = ftell(m4aFile->sizeStream);
	return (offset - m4aFile->sizeOffset) / 4;	/* 4 byte size-values */
}

bool m4aFileHasMoreSamples(M4AFile *m4aFile) {
	return m4aFileGetCurrentSampleIndex(m4aFile) < m4aFile->samplesCount;
}

bool m4aFileGetNextSample(M4AFile *m4aFile, uint8_t *sampleBuffer, uint32_t *sampleSize) {
	uint32_t dataSize;

	/* Read next sample size */
	dataSize = read4ByteUnsignedInt32(m4aFile->sizeStream);
	if(didReadErrorOccur(m4aFile->sizeStream)) {
		return false;
	}

	/* Read next sample */
	if(!m4aFileReadData(m4aFile, sampleBuffer, dataSize)) {
		return false;
	}
	*sampleSize = dataSize;

	return true;
}

bool m4aFileClose(M4AFile **m4aFile) {
	bool result;

	/* Close all opened/allocated resource. Continu if a failure occurs, but remember failure for final result. */
	result = true;
	if(*m4aFile != NULL) {
		if((*m4aFile)->dataStream != NULL) {
			if(fclose((*m4aFile)->dataStream) != 0) {
				logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot close datastream. (errno = %d)", errno);
				result = false;
			}
		}
		if((*m4aFile)->sizeStream != NULL) {
			if(fclose((*m4aFile)->sizeStream) != 0) {
				logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot close sizestream. (errno = %d)", errno);
				result = false;
			}
		}
		if(!bufferFree(m4aFile)) {
			result = false;
		}
	}

	return result;
}

/* Internal functions */
void m4aFileInitialize(M4AFile *m4aFile) {
	/* Reset all fields of m4aFile */
	m4aFile->dataStream = NULL;
	m4aFile->sizeStream = NULL;
	m4aFile->dataOffset = UNUSED_OFFSET;
	m4aFile->sizeOffset = UNUSED_OFFSET;
	m4aFile->totalSize = 0;
	m4aFile->largestSampleSize = 0;
	m4aFile->timescale = 0;
	m4aFile->duration = 0;
	m4aFile->encoding = ENCODING_UNKNOWN;
	m4aFile->status = M4AFILE_OK;
	m4aFile->metadataHandler = NULL;
}

void m4aFileSetTimescale(M4AFile *m4aFile, uint32_t timescale) {
	m4aFileSetTimeValue(m4aFile, timescale, &m4aFile->timescale, "timescale");
}

void m4aFileSetDuration(M4AFile *m4aFile, uint32_t duration) {
	m4aFileSetTimeValue(m4aFile, duration, &m4aFile->duration, "duration");
}

void m4aFileSetTimeValue(M4AFile *m4aFile, uint32_t timeValue, uint32_t *timeField, const char *timeFieldName) {
	/* Only set the timeValue if it is (somewhat) meaningful */
	if(timeValue != 0 && timeValue != 0xffffffff) {

		/* Is this a new timeValue? */
		if(*timeField != timeValue) {
			if(*timeField == 0) {
				*timeField = timeValue;
			} else {
				logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Parser: Multiple different %s values are present. Continuing with the latest value '%" PRIu32 "'.", timeFieldName, timeValue);
				m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
			}
		}
	}
}

bool setTotalSampleSize(M4AFile *m4aFile, uint32_t totalSampleSize) {
	if(m4aFile->totalSampleSize == 0) {
		m4aFile->totalSampleSize = totalSampleSize;
	} else if(m4aFile->totalSampleSize != totalSampleSize) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Parser: More than 2 different sample size values are present. Continuing but playback might be cut off.");

		/* Use the smaller value when provided value is less than stored one */
		if(totalSampleSize < m4aFile->totalSampleSize) {
			m4aFile->totalSampleSize = totalSampleSize;
		}
	}
	return true;
}

bool m4aFileSetDataOffset(M4AFile *m4aFile) {
	return m4aFileSetOffsetValue(m4aFile, &m4aFile->dataOffset, "data");
}

bool m4aFileSetSizeOffset(M4AFile *m4aFile) {
	return m4aFileSetOffsetValue(m4aFile, &m4aFile->sizeOffset, "size");
}

bool m4aFileSetOffsetValue(M4AFile *m4aFile, uint32_t *offsetField, const char *offsetFieldName) {
	/* The dataStream is used during parsing, so its current position denotes the offset */
	*offsetField = ftell(m4aFile->dataStream);
	if(ferror(m4aFile->dataStream)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot retrieve the location of the %s table box from the file stream. (errno = %d)", offsetFieldName, errno);
		m4aFile->status = M4AFILE_ERROR;
		return false;
	}
	return true;
}

uint32_t mp4BoxParse(M4AFile *m4aFile, uint32_t containerBoxType) {
	uint32_t boxSize;
	uint32_t boxBytesRead;
	uint32_t boxType;
	int index;

	/* Read box size. Check if there was anything to read or whether error has occured. */
	if(!m4aFileReadUnsignedLong(m4aFile, containerBoxType, &boxSize)) {
		if(feof(m4aFile->dataStream)) {
			/* Little hack: Assume no byte was read and the EOF is reached and no error occured. */
			/* Set the status to M4AFILE_OK to indicate everything is okay. */
			/* The situation that (up to 3) superfluous bytes are present at the end of the file */
			/* is not detected properly. This seems an acceptable tradeof for not having to perform */
			/* extra checks. */
			m4aFile->status = M4AFILE_OK;
		} else {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read new box size inside box \"%" PRIls32 "\".", containerBoxType);
		}
		return 0;
	}

	/* Read box type. Check if there was anything to read or whether error has occured. */
	if(!m4aFileReadUnsignedLong(m4aFile, containerBoxType, &boxType)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read new box type inside box \"%" PRIls32 "\".", containerBoxType);
		return 0;
	}

	/* We read 2x 4 bytes so far */
	boxBytesRead = 8;

	/* Find the box-specific parser and let it do its work */
	/* Searching is done sequentially. The array is small and accessed probably less than 100 times. */
	/* Optimising the search algorithm does not improve readability and will not increase the performance noticably. */
	index = 0;
	while(mp4BoxParserTable[index].type != 0 && mp4BoxParserTable[index].type != boxType) {
		index++;
	}
	if(mp4BoxParserTable[index].type == boxType) {
		logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsing box \"%" PRIls32 "\" with a total size of %" PRIi32 " bytes.", INT32_TO_ASCII(boxType), boxSize);
		boxBytesRead += mp4BoxParserTable[index].boxParser(m4aFile, boxType, boxSize - boxBytesRead);
	}

	/* Do we need to read some trailer data? Because no box-specific parser is found or parser failed to read all. This should not occur. */
	if(boxBytesRead < boxSize) {

		/* If an unknown box type is read. Skip the box completely. Hopefully no information is lost. */
		if(mp4BoxParserTable[index].type == 0) {
			logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Internal: A MP4 box of type \"%" PRIls32 "\" is not known by the parser. This should not occur. The box content (%" PRIu32 " bytes) is skipped.", INT32_TO_ASCII(boxType), boxSize);
			m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
		} else {
			logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Internal: A MP4 box of type \"%" PRIls32 "\" is not read completely by its box-specific parser or the parser returned an invalid value. This should not occur. The remainder of the box content (%" PRIu32 ") is skipped.", INT32_TO_ASCII(boxType), boxSize - boxBytesRead);
			m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
		}
		if(!m4aFileSkipBytes(m4aFile, boxType, boxSize - boxBytesRead)) {
			return 0;
		}
		boxBytesRead = boxSize;
	} else if(boxBytesRead > boxSize) {
		/* The box-specific parser read more than expected. Continuing will most likely fail, but try anyway. */
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Internal: Parsing a MP4 box of type \"%" PRIls32 "\" resulted in more data than expected. This should not occur. Continuing, but parsing might fail.", INT32_TO_ASCII(boxType));
		m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
	}

	return boxBytesRead;
}

uint32_t mp4BoxParseFileType(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	uint32_t mainType;
	uint32_t mainVersion;

	/* Read main type and version. Check the values. */
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &mainType)) {
		return 0;
	}
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &mainVersion)) {
		return 0;
	}
	if(mainType != APPLE_FILE_TYPE || mainVersion != 0x00000000) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Parser: Unknown file type \"%" PRIls32 "\" or unknown version 0x%" PRIx32 " found in box \"%" PRIls32 "\" (expecting \"%" PRIls32 "\", 0x0). Continuing, but parsing might fail.", INT32_TO_ASCII(mainType), mainVersion, INT32_TO_ASCII(boxType), INT32_TO_ASCII(APPLE_FILE_TYPE));
		m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
	}

	/* Skip remaining compatible brands */
	if(!m4aFileSkipBytes(m4aFile, boxType, boxBytesLeft - 8)) {
		return 0;
	}

	/* Write info from this box */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed box \"%" PRIls32 "\", content size %" PRIu32 ", main type \"%" PRIls32 "\", version %" PRIu32 ".", INT32_TO_ASCII(boxType), boxBytesLeft, INT32_TO_ASCII(mainType), mainVersion);

	return boxBytesLeft;
}

uint32_t mp4BoxParseMediaHeader(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	uint8_t boxVersion;
	uint32_t timescale;
	uint32_t duration;

	/* Check version and flags (All bits 0's) */
	if(!m4aFileCheckVersionAndFlags(m4aFile, boxType, &boxVersion, 0x00, NULL, 0x00000000, 0x00ffffff)) {
		return 0;
	}

	/* Check if there is enough content in the box */
	if(boxBytesLeft < (boxVersion == 0 ? 20 : 32)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Not enough data in box \"%" PRIls32 "\".", INT32_TO_ASCII(boxType));
		m4aFile->status = M4AFILE_ERROR;
		return 0;
	}

	/* Skip creation and modification time. For version 0 2x 4 bytes, for version 1 2x 8 bytes. */
	if(!m4aFileSkipBytes(m4aFile, boxType, boxVersion == 0 ? 8 : 16)) {
		return 0;
	}

	/* Read timescale */
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &timescale)) {
		return 0;
	}
	m4aFileSetTimescale(m4aFile, timescale);

	/* Read duration */
	if(!m4aFileReadDuration(m4aFile, boxType, boxVersion, &duration)) {
		return 0;
	}

	/* Skip rest of the box content (containing things like: volume, window geometry, timing and next track id) */
	if(!m4aFileSkipBytes(m4aFile, boxType, boxBytesLeft - (boxVersion == 0 ? 20 : 32))) {
		return 0;
	}

	/* Write info from this box */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed box \"%" PRIls32 "\", content size %" PRIu32 ", timescale %" PRIu32 ", duration %" PRIu32 " (%s).", INT32_TO_ASCII(boxType), boxBytesLeft, timescale, (timescale > 0 ? duration / timescale : duration), (timescale > 0 ? "seconds" : "<unknown timescale>"));

	return boxBytesLeft;
}

uint32_t mp4BoxParseTrackHeader(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	uint8_t boxVersion;
	uint32_t undocumentedDuration;
	uint32_t duration;

	/* Check version and flags (3 lowest bits are used, but seemingly unimportant here) */
	if(!m4aFileCheckVersionAndFlags(m4aFile, boxType, &boxVersion, 0x00, NULL, 0x00000000, 0x00fffff8)) {
		return 0;
	}

	/* Check if there is enough content in the box */
	if(boxBytesLeft < (boxVersion == 0 ? 28 : 40)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Not enough data in box \"%" PRIls32 "\".", INT32_TO_ASCII(boxType));
		m4aFile->status = M4AFILE_ERROR;
		return 0;
	}

	/* Skip creation and modification time. For version 0 2x 4 bytes, for version 1 2x 8 bytes. */
	/* Also skip 4 byte track id and 4 byte reserved value */
	if(!m4aFileSkipBytes(m4aFile, boxType, boxVersion == 0 ? 16 : 24)) {
		return 0;
	}

	/* The following 4 bytes are also reserved (according to MP4 spec), but seems to contain the duration (in timescale units) */
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &undocumentedDuration)) {
		return 0;
	}
	m4aFileSetDuration(m4aFile, undocumentedDuration);

	/* Read duration */
	if(!m4aFileReadDuration(m4aFile, boxType, boxVersion, &duration)) {
		return 0;
	}

	/* Skip rest of the box content (containing things like: volume, window geometry, timing and next track id) */
	if(!m4aFileSkipBytes(m4aFile, boxType, boxBytesLeft - (boxVersion == 0 ? 28 : 40))) {
		return 0;
	}

	/* Write info from this box */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed box \"%" PRIls32 "\", content size %" PRIu32 ", undocumented duration %" PRIu32 ", duration %" PRIu32 " (durations in %s).", INT32_TO_ASCII(boxType), boxBytesLeft, (m4aFile->timescale > 0 ? undocumentedDuration / m4aFile->timescale : undocumentedDuration), (m4aFile->timescale > 0 ? duration / m4aFile->timescale : duration), (m4aFile->timescale > 0 ? "seconds" : "<unknown timescale>"));

	return boxBytesLeft;
}

uint32_t mp4BoxParseSampleDescriptions(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	uint32_t boxBytesRead;

	/* Check version and flags (All bits 0's) */
	if(!m4aFileCheckVersionAndFlags(m4aFile, boxType, NULL, 0x00, NULL, 0x00000000, 0x00ffffff)) {
		return 0;
	}

	/* Skip description count */
	if(!m4aFileSkipBytes(m4aFile, boxType, 4)) {
		return 0;
	}

	/* Read 8 bytes so far */
	boxBytesRead = 8;

	/* Read description box(es) */
	boxBytesRead += mp4BoxParseContainerInternal(m4aFile, boxType, boxBytesLeft - boxBytesRead, &mp4BoxParse);

	return boxBytesRead;
}

uint32_t mp4BoxParseSampleDescription(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	/* Set encoding based on the box type */
	if(boxType == ALAC_ENCODING_TYPE) {
		if(m4aFile->encoding != ENCODING_UNKNOWN && m4aFile->encoding != ENCODING_ALAC) {
			logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Parser: Read contradicting encodings in file (ie both ALAC and AAC). Continuing with first encoding found.");
			m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
		} else {
			logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsing box \"%" PRIls32 "\", therefore set encoding to ALAC.", INT32_TO_ASCII(boxType));
			m4aFile->encoding = ENCODING_ALAC;
		}
	} else if(boxType == AAC_ENCODING_TYPE) {
		if(m4aFile->encoding != ENCODING_UNKNOWN && m4aFile->encoding != ENCODING_AAC) {
			logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Parser: Read contradicting encodings in file (ie both ALAC and AAC). Continuing with first encoding found.");
			m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
		} else {
			logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsing box \"%" PRIls32 "\", therefore set encoding to AAC.", INT32_TO_ASCII(boxType));
			m4aFile->encoding = ENCODING_AAC;
			m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
		}
	}
	return mp4BoxSkip(m4aFile, boxType, boxBytesLeft);
}

uint32_t mp4BoxParseSampleTimes(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	uint32_t boxBytesRead;
	uint32_t numberOfTimings;
	uint32_t i;
	uint32_t frameCount;
	uint32_t duration;
	uint32_t totalDuration;

	/* Check version and flags (All bits 0's) */
	if(!m4aFileCheckVersionAndFlags(m4aFile, boxType, NULL, 0x00, NULL, 0x00000000, 0x00ffffff)) {
		return 0;
	}

	/* Get the number of timings (ie count) */
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &numberOfTimings)) {
		return 0;
	}

	/* Read 8 bytes so far */
	boxBytesRead = 8;

	/* Read the individual timings (durations) */
	totalDuration = 0;
	for(i = 0; i < numberOfTimings; i++) {
		if(!m4aFileReadUnsignedLong(m4aFile, boxType, &frameCount)) {
			return 0;
		}
		if(!m4aFileReadUnsignedLong(m4aFile, boxType, &duration)) {
			return 0;
		}
		totalDuration += frameCount * duration;
	}
	m4aFileSetDuration(m4aFile, totalDuration);
	boxBytesRead += numberOfTimings * 8;

	/* Write info from this box */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed box \"%" PRIls32 "\", content size %" PRIu32 ", duration %" PRIu32 " (%s).", INT32_TO_ASCII(boxType), boxBytesLeft, (m4aFile->timescale > 0 ? totalDuration / m4aFile->timescale : totalDuration), (m4aFile->timescale > 0 ? "seconds" : "<unknown timescale>"));

	return boxBytesRead;
}

uint32_t mp4BoxParseSampleSizes(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	uint32_t boxBytesRead;
	uint32_t samplesCount;
	uint32_t sampleSizeForAll;
	uint32_t sampleSize;
	uint32_t totalSampleSize;
	uint32_t largestSampleSize;
	uint32_t i;

	/* Check version and flags (All bits 0's) */
	if(!m4aFileCheckVersionAndFlags(m4aFile, boxType, NULL, 0x00, NULL, 0x00000000, 0x00ffffff)) {
		return 0;
	}

	/* Get the size of all samples (it is assumed 0, because every sample will get its own size). */
	/* Probably a non-zero value for uncompressed variants, not investigated further. */
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &sampleSizeForAll)) {
		return 0;
	}
	if(sampleSizeForAll != 0) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "The (fixed) sample size for all samples is defined as %" PRIu32 ", expected 0. Continuing, but parsing might fail.", sampleSizeForAll);
		m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
	}

	/* Get the number of samples */
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &samplesCount)) {
		return 0;
	}
	m4aFile->samplesCount = samplesCount;

	/* Read 12 bytes so far */
	boxBytesRead = 12;

	/* Store the offset in the file for later usage */
	if(!m4aFileSetSizeOffset(m4aFile)) {
		return 0;
	}

	/* Read all sample sizes and store largest size */
	totalSampleSize = 0;
	largestSampleSize = 0;
	for(i = 0; i < samplesCount; i++) {
		if(!m4aFileReadUnsignedLong(m4aFile, boxType, &sampleSize)) {
			return 0;
		}
		totalSampleSize += sampleSize;
		if(largestSampleSize < sampleSize) {
			largestSampleSize = sampleSize;
		}
	}
	if(!setTotalSampleSize(m4aFile, totalSampleSize)) {
		return 0;
	}
	m4aFile->largestSampleSize = largestSampleSize;
	boxBytesRead += samplesCount * 4;

	/* Write info from this box */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed box \"%" PRIls32 "\", content size %" PRIu32 ", sample count %" PRIu32 ", total sample size %" PRIu32 ", largest sample size %" PRIu32 ".", INT32_TO_ASCII(boxType), boxBytesLeft, samplesCount, totalSampleSize, largestSampleSize);

	return boxBytesRead;
}

uint32_t mp4BoxParseMetadata(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	uint32_t boxBytesRead;

	/* Check version and flags (All bits 0's) */
	if(!m4aFileCheckVersionAndFlags(m4aFile, boxType, NULL, 0x00, NULL, 0x00000000, 0x00ffffff)) {
		return 0;
	}

	/* Read 4 bytes so far */
	boxBytesRead = 4;

	/* Read the individual metadata boxes */
	boxBytesRead += mp4BoxParseContainerInternal(m4aFile, boxType, boxBytesLeft - boxBytesRead, &mp4BoxParse);

	return boxBytesRead;
}

uint32_t mp4BoxParseAppleAnnotation(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	return mp4BoxParseContainerInternal(m4aFile, boxType, boxBytesLeft, &mp4BoxParseAppleData);
}

uint32_t mp4BoxParseAppleData(M4AFile *m4aFile, uint32_t annotationBoxType) {
	uint32_t boxFlags;
	uint32_t boxSize;
	uint32_t boxBytesRead;
	uint32_t boxType;
	bool isDataBox;

	/* Read box size. Check if there was anything to read or whether error has occured. */
	if(!m4aFileReadUnsignedLong(m4aFile, annotationBoxType, &boxSize)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read new box size inside box \"%" PRIls32 "\".", annotationBoxType);
		return 0;
	}

	/* Read box type. Check if there was anything to read or whether error has occured. */
	isDataBox = false;
	if(!m4aFileReadUnsignedLong(m4aFile, annotationBoxType, &boxType)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read new box type inside box \"%" PRIls32 "\".", annotationBoxType);
		return 0;
	}
	if(boxType == METADATA_DATA_TYPE) {
		isDataBox = true;
	} else if(boxType != METADATA_MEAN_TYPE && boxType != METADATA_NAME_TYPE) {
		logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Internal: An Apple data box with type \"%" PRIls32 "\" is read, but \"%" PRIls32 "\", \"%" PRIls32 "\" or \"%" PRIls32 "\" is expected. Continuing but parsing might fail.", INT32_TO_ASCII(boxType), INT32_TO_ASCII(METADATA_DATA_TYPE), INT32_TO_ASCII(METADATA_NAME_TYPE), INT32_TO_ASCII(METADATA_MEAN_TYPE));
	}

	/* Check version and flags (5 lowest bits are used for metadata type) */
	if(!m4aFileCheckVersionAndFlags(m4aFile, boxType, NULL, 0x00, &boxFlags, 0x00000000, 0x00ffffe0)) {
		return 0;
	}

	/* We read 12 bytes so far */
	boxBytesRead = 12;

	/* In box 'data' skip 4 bytes */
	if(isDataBox) {
		if(!m4aFileSkipBytes(m4aFile, boxType, 4)) {
			return 0;
		}
		boxBytesRead += 4;
	}

	/* The remainder of the box is the data content */
	if(boxBytesRead < boxSize) {
		if(!m4aFileReadMetadataContent(m4aFile, annotationBoxType, boxType, boxFlags, boxSize - boxBytesRead)) {
			return 0;
		}
		boxBytesRead = boxSize;
	} else if(boxBytesRead > boxSize) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Internal: Read more data in Apple data box(es) than container \"%" PRIls32 "\" specified. Read %" PRIu32 " expected %" PRIu32 ". Continuing, but parsing might fail.", INT32_TO_ASCII(boxType), boxBytesRead, boxSize);
		m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
	}

	return boxBytesRead;

}

uint32_t mp4BoxParseMediaData(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	/* Set offset and size for dataStream */
	if(!m4aFileSetDataOffset(m4aFile)) {
		return 0;
	}
	if(!setTotalSampleSize(m4aFile, boxBytesLeft)) {
		return 0;
	}

	/* Skip data */
	return mp4BoxSkip(m4aFile, boxType, boxBytesLeft);
}

uint32_t mp4BoxSkip(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed box \"%" PRIls32 "\" by skipping all data (no need for content).", INT32_TO_ASCII(boxType));
	if(!m4aFileSkipBytes(m4aFile, boxType, boxBytesLeft)) {
		return 0;
	}
	return boxBytesLeft;
}

uint32_t mp4BoxParseContainer(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft) {
	return mp4BoxParseContainerInternal(m4aFile, boxType, boxBytesLeft, &mp4BoxParse);
}

uint32_t mp4BoxParseContainerInternal(M4AFile *m4aFile, uint32_t boxType, uint32_t boxBytesLeft, mp4BoxParserGeneric boxParser) {
	uint32_t containerSize;
	uint32_t boxCount;

	/* Repeatedly read boxes until container's size is reached */
	containerSize = 0;
	boxCount = 0;
	while(m4aFile->status != M4AFILE_ERROR && containerSize < boxBytesLeft) {
		containerSize += boxParser(m4aFile, boxType);
		boxCount++;
	}

	/* Did the box sizes match up to the container size (but not more)? */
	if(m4aFile->status != M4AFILE_ERROR && containerSize > boxBytesLeft) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Parser: Read more data in box(es) than container \"%" PRIls32 "\" specified. Read %" PRIu32 " expected %" PRIu32 ". Continuing, but parsing might fail.", INT32_TO_ASCII(boxType), containerSize, boxBytesLeft);
		m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
	}

	/* Write info from this box */
	logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed box \"%" PRIls32 "\", content size %" PRIu32 ", boxes read %" PRIu32 " (not including sub-boxes).", INT32_TO_ASCII(boxType), containerSize, boxCount);

	return containerSize;
}

bool m4aFileReadDuration(M4AFile *m4aFile, uint32_t boxType, uint8_t boxVersion, uint32_t *duration) {
	uint32_t value;
	bool hasUnknownDuration;

	/* In case box version == 0x01, the duration is 64 bit value. Such large values can't be handled here, */
	/* so check whether first 32 bit are all 0's or all 1's, in which case we simply use lowest 32 bit. */
	/* All 1's will most likely mean that the lowest 32 bits are also all 1's, in which case the duration is 'unknown'. */
	hasUnknownDuration = false;
	if(boxVersion == 0x01) {
		if(!m4aFileReadUnsignedLong(m4aFile, boxType, &value)) {
			return false;
		}
		if(value == 0xffffffff) {
			hasUnknownDuration = true;
		} else if(value != 0) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot handle duration values (of 64 bits) larger than 0x00000000ffffffff in box \"%" PRIls32 "\".", INT32_TO_ASCII(boxType));
			return false;
		}
	}
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &value)) {
		return false;
	}
	if(value != 0xffffffff && hasUnknownDuration) {	/* Oops, not all 1's in second 32 bits */
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot handle duration values (of 64 bits) larger than 0x00000000ffffffff in box \"%" PRIls32 "\".", INT32_TO_ASCII(boxType));
		return false;
	}
	m4aFileSetDuration(m4aFile, value);
	*duration = value;
	return true;
}

bool m4aFileReadMetadataContent(M4AFile *m4aFile, uint32_t annotationBoxType, uint32_t boxType, uint32_t metadataType, uint32_t dataSize) {
	uint8_t *data;

	/* If a metadataHandler exists, actually read the data. Otherwise, just skip it. */
	if(m4aFile->metadataHandler != NULL) {
		if(!bufferAllocate(&data, sizeof(uint8_t) * dataSize, "metadata buffer")) {
			m4aFile->status = M4AFILE_ERROR;
			return false;
		}
		if(!m4aFileReadData(m4aFile, data, dataSize)) {
			bufferFree(&data);
			return false;
		}

		/* Handle metadata */
		logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed metadata for box \"%" PRIls32 "\" (%" PRIu32 " bytes). Delegate processing to metadata handler.", INT32_TO_ASCII(annotationBoxType), dataSize);
		m4aFile->metadataHandler(annotationBoxType == ITUNES_ANNOTATION_TYPE ? boxType : annotationBoxType, data, dataSize, metadataType);

		/* Clean up */
		if(!bufferFree(&data)) {
			return false;
		}
	} else {
		logWrite(LOG_LEVEL_DEBUG, LOG_COMPONENT_NAME, "Parsed metadata for box \"%" PRIls32 "\" (%" PRIu32 " bytes). Skipping content, since no metadata handler is present.", INT32_TO_ASCII(annotationBoxType), dataSize);
		if(!m4aFileSkipBytes(m4aFile, boxType, dataSize)) {
			return false;
		}
	}

	return true;
}

/* General reading functions. Assuming 'dataStream' is in use. */
bool m4aFileSkipBytes(M4AFile *m4aFile, uint32_t boxType, uint32_t byteCount) {
	if(fseek(m4aFile->dataStream, byteCount, SEEK_CUR) != 0) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot seek past (%" PRIu32 ") unused bytes for box \"%" PRIls32 "\". (errno = %d)", byteCount, INT32_TO_ASCII(boxType), errno);
		m4aFile->status = M4AFILE_ERROR;
		return false;
	}
	return true;
}

bool m4aFileCheckVersionAndFlags(M4AFile *m4aFile, uint32_t boxType, uint8_t *boxVersion, uint8_t expectedVersion, uint32_t *boxFlags, uint32_t expectedFlagBitsOn, uint32_t expectedFlagBitsOff) {
	uint32_t versionAndFlags;
	uint8_t version;
	uint32_t flags;

	/* Read 4 bytes containing version (1 byte) and flags (3 bytes) */
	if(!m4aFileReadUnsignedLong(m4aFile, boxType, &versionAndFlags)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read version and flag information for box \"%" PRIls32 "\".", INT32_TO_ASCII(boxType));
		return false;
	}

	/* Check for expected version number */
	version = (versionAndFlags & 0xff000000) >> 24;
	if(version != expectedVersion) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Parser: Read version byte for box \"%" PRIls32 "\" returned 0x%" PRIx8 ", expected 0x%" PRIx8 ". Continuing, but parsing might fail.", INT32_TO_ASCII(boxType), version, expectedVersion);
		m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
	}

	/* If a valid boxVersion pointer is given, store actual value there */
	if(boxVersion != NULL) {
		*boxVersion = version;
	}

	/* Check presence certain bit-values in the flags field */
	flags = versionAndFlags & 0x00ffffff;
	if((flags & expectedFlagBitsOn) != expectedFlagBitsOn || (flags & expectedFlagBitsOff) != 0) {
		logWrite(LOG_LEVEL_WARNING, LOG_COMPONENT_NAME, "Parser: Read flags for box \"%" PRIls32 "\" returned 0x%" PRIx32 ", expected bits ON mask 0x%" PRIx32 " and expected bits OFF mask 0x%" PRIx32 ". Continuing, but parsing might fail.", INT32_TO_ASCII(boxType), flags, expectedFlagBitsOn, expectedFlagBitsOff);
		m4aFile->status = M4AFILE_PARSED_WITH_WARNINGS;
	}

	/* If a valid boxFlags pointer is given, store actual value there */
	if(boxFlags != NULL) {
		*boxFlags = flags;
	}

	return true;
}

bool m4aFileReadUnsignedLong(M4AFile *m4aFile, uint32_t boxType, uint32_t *result) {
	uint32_t value = read4ByteUnsignedInt32(m4aFile->dataStream);
	if(feof(m4aFile->dataStream)) {
		m4aFile->status = M4AFILE_ERROR;	/* See mp4BoxParse for explanation what happens when no data is present (ie EOF is reached) */
		return false;
	} else if(ferror(m4aFile->dataStream)) {
		logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read data for box \"%" PRIls32 "\". (errno = %d)", INT32_TO_ASCII(boxType), errno);
		m4aFile->status = M4AFILE_ERROR;
		return false;
	}
	*result = value;
	return true;
}

bool m4aFileReadData(M4AFile *m4aFile, uint8_t *data, uint32_t dataSize) {
	if(fread(data, sizeof(uint8_t), dataSize, m4aFile->dataStream) != dataSize) {
		if(feof(m4aFile->dataStream)) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read data (%" PRIu32 " bytes), end of file reached prematurely.", dataSize);
		} else if(ferror(m4aFile->dataStream)) {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read data (%" PRIu32 " bytes). (errno = %d)", dataSize, errno);
		} else {
			logWrite(LOG_LEVEL_ERROR, LOG_COMPONENT_NAME, "Cannot read data (%" PRIu32 " bytes), less bytes read than requested.", dataSize);
		}
		m4aFile->status = M4AFILE_ERROR;
		return false;
	}
	return true;
}

/* File reading functions. No error checking is done here. */
uint32_t read4ByteUnsignedInt32(FILE *stream) {
	/* Read uint32_t byte for byte (in network byte order) */
	/* FILE operations are buffered, performance between single call to fread(value, 1, 4, stream) and multiple calls */
	/* to fgetc is comparable in test-environment. */
	uint32_t result = (uint32_t)fgetc(stream);
	result <<= 8;
	result |= (uint32_t)fgetc(stream);
	result <<= 8;
	result |= (uint32_t)fgetc(stream);
	result <<= 8;
	result |= (uint32_t)fgetc(stream);
	return result;
}

/* Error checking */
bool didReadErrorOccur(FILE *stream) {
	if(feof(stream) || ferror(stream)) {
		return true;
	}
	return false;
}
