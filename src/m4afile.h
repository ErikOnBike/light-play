/*
 * File: m4afile.h
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

#ifndef	__M4AFILE_H__
#define	__M4AFILE_H__

#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <time.h>

/* Type definition for M4AFile */
typedef struct M4AFileStruct M4AFile;

/* Type definition for audio encodings in M4A files */
typedef enum {
	ENCODING_UNKNOWN = 0,
	ENCODING_ALAC = 1,
	ENCODING_AAC = 2
} M4AFileEncoding;

/* Type definition for type of metadata in M4A files */
typedef enum {
	METADATA_DATA = 0x00,
	METADATA_TEXT = 0x01,
	METADATA_BOOLEAN = 0x15,
	METADATA_IMAGE = 0x0D
} M4AFileMetadataType;

/* Type definition for metadata handler of M4A files */
typedef void (*M4AFileMetadataHandler)(uint32_t boxType, uint8_t *buffer, uint32_t bufferSize, M4AFileMetadataType metadataType);

/*
 * Function: m4aFileOpen
 * Parameters:
 *	fileName - path to the M4A file to open
 * Returns: a M4AFile structure to further access the M4A file or NULL if opening the file is unsuccessful
 */
M4AFile *m4aFileOpen(const char *fileName);

/*
 * Function: m4aFileSetMetadataHandler
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen)
 *	metadataHandler - handler which will be called for every metadata encountered in the M4A file
 * Returns: a boolean specifying if setting the metadata handler is successful
 */
bool m4aFileSetMetadataHandler(M4AFile *m4aFile, M4AFileMetadataHandler metadataHandler);

/*
 * Function: m4aFileParse
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen)
 * Returns: a boolean specifying if parsing the M4A file is successful
 *
 * Remarks:
 * Parsing a M4A file might result in warnings being logged. If no errors are encountered during parsing
 * the function will return success. After parsing the warning status can be retrieved using the function
 * hasM4AFileParsedWithWarnings. Warnings can be the result of @@.
 * Parsing the M4A file will read the necessary information from the file to be able to stream audio data
 * from it. This information can be retrieved using the different getters (shown below).
 * If a metadata handler is set (by setM4AFileMetadataHandler), this handler is called for all metadata
 * read during parsing. The iTunes specific metadata in the '----' annotation box is handled by consecutively
 * calling the handler with the 'mean', 'name' and 'data' values. (see the following website for M4A
 * metadata as well as parsing these files: http://atomicparsley.sourceforge.net/mpeg-4files.html)
 */
bool m4aFileParse(M4AFile *m4aFile);

/*
 * Function: m4aFileHasParsedWithWarnings
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 * Returns: a boolean specifying if parsing the M4A file resulted in success, but warnings were logged.
 */
bool m4aFileHasParsedWithWarnings(M4AFile *m4aFile);

/*
 * Function: m4aFileGetEncoding
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 * Returns: an M4AFileEncoding specifying the encoding type of the M4A file
 */
M4AFileEncoding m4aFileGetEncoding(M4AFile *m4aFile);

/*
 * Function: m4aFileGetTimescale
 * Parameters:
 *	m4aFile - already open M4A file (as returned by openM4AFile) which is parsed (by parseM4AFile)
 * Returns: a 4-byte unsigned integer specifying the timescale (number of samples per second) of the M4A file
 */
uint32_t m4aFileGetTimescale(M4AFile *m4aFile);

/*
 * Function: m4aFileGetDuration
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 *	time - length of audio file in (struct timespec)
 * Returns: a boolean specifying if getting the length of the M4A file is successful
 */
bool m4aFileGetLength(M4AFile *m4aFile, struct timespec *time);

/*
 * Function: m4aFileGetSamplesCount
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 * Returns: a 4-byte unsigned integer specifying the number of samples in the m4aFile
 */
uint32_t m4aFileGetSamplesCount(M4AFile *m4aFile);

/*
 * Function: m4aFileGetLargestSampleSize
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 * Returns: a 4-byte unsigned integer specifying the size (in bytes) of the largest sample
 */
uint32_t m4aFileGetLargestSampleSize(M4AFile *m4aFile);

/*
 * Function: m4aFileSetSampleOffset
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 *	timeOffset - offset (in seconds and nanoseconds) where 'getM4AFileNextSample' will begin providing samples
 * Returns: a boolean specifying if setting the offset of the M4A file is successful
 */
bool m4aFileSetSampleOffset(M4AFile *m4aFile, struct timespec *timeOffset);

/*
 * Function: m4aFileGetCurrentSampleIndex
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 * Returns: the index (number) of the next sample to be returned by m4aFileGetNextSample
 */
uint32_t m4aFileGetCurrentSampleIndex(M4AFile *m4aFile);

/*
 * Function: m4aFileHasMoreSamples
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 * Returns: a boolean specifying if more samples are present in the M4A file (ie if calling m4aFileGetNextSample is possible)
 */
bool m4aFileHasMoreSamples(M4AFile *m4aFile);

/*
 * Function: m4aFileGetNextSample
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen) which is parsed (by m4aFileParse)
 *	sampleBuffer - byte array large enough (see getM4AFileLargestSampleSize) to hold sample
 * Returns: a boolean specifying if retrieving the next sample from the M4A file is successful
 */
bool m4aFileGetNextSample(M4AFile *m4aFile, uint8_t *sampleBuffer, uint32_t *sampleSize);

/*
 * Function: m4aFileClose
 * Parameters:
 *	m4aFile - already open M4A file (as returned by m4aFileOpen)
 * Returns: a boolean specifying if closing the M4A file is successful
 *
 * Remarks:
 * Close the M4A file and free any opened/allocated resources.
 */
bool m4aFileClose(M4AFile **m4aFile);

#endif	/* __M4AFILE_H__ */
