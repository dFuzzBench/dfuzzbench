// SPDX-License-Identifier: GPL-2.0-or-later

/*
  File:      epsimage.cpp
  Author(s): Michael Ulbrich (mul) <mul@rentapacs.de>
             Volker Grabsch (vog) <vog@notjusthosting.com>
  History:   7-Mar-2011, vog: created
 */
// *****************************************************************************
// included header files
#include "config.h"

#include "basicio.hpp"
#include "enforce.hpp"
#include "epsimage.hpp"
#include "error.hpp"
#include "futils.hpp"
#include "image.hpp"
#include "utils.hpp"
#include "version.hpp"

// + standard includes
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <sstream>
#include <string>

// *****************************************************************************
namespace {
using namespace Exiv2;
using namespace Exiv2::Internal;

// signature of DOS EPS
constexpr auto dosEpsSignature = std::string_view("\xC5\xD0\xD3\xC6");

// first line of EPS
constexpr std::string_view epsFirstLine[] = {
    "%!PS-Adobe-3.0 EPSF-3.0",
    "%!PS-Adobe-3.0 EPSF-3.0 ",  // OpenOffice
    "%!PS-Adobe-3.1 EPSF-3.0",   // Illustrator
};

// blank EPS file
constexpr auto epsBlank = std::string_view(
    "%!PS-Adobe-3.0 EPSF-3.0\n"
    "%%BoundingBox: 0 0 0 0\n");

// list of all valid XMP headers
constexpr std::string_view xmpHeaders[] = {

    // We do not enforce the trailing "?>" here, because the XMP specification
    // permits additional attributes after begin="..." and id="...".

    // normal headers
    "<?xpacket begin=\"\xef\xbb\xbf\" id=\"W5M0MpCehiHzreSzNTczkc9d\"",
    "<?xpacket begin=\"\xef\xbb\xbf\" id='W5M0MpCehiHzreSzNTczkc9d'",
    "<?xpacket begin='\xef\xbb\xbf' id=\"W5M0MpCehiHzreSzNTczkc9d\"",
    "<?xpacket begin='\xef\xbb\xbf' id='W5M0MpCehiHzreSzNTczkc9d'",

    // deprecated headers (empty begin attribute, UTF-8 only)
    "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"",
    "<?xpacket begin=\"\" id='W5M0MpCehiHzreSzNTczkc9d'",
    "<?xpacket begin='' id=\"W5M0MpCehiHzreSzNTczkc9d\"",
    "<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d'",
};

// list of all valid XMP trailers
using XmpTrailer = std::pair<std::string_view, bool>;

constexpr auto xmpTrailers = std::array{

    // We do not enforce the trailing "?>" here, because the XMP specification
    // permits additional attributes after end="...".

    XmpTrailer("<?xpacket end=\"r\"", true),
    XmpTrailer("<?xpacket end='r'", true),
    XmpTrailer("<?xpacket end=\"w\"", false),
    XmpTrailer("<?xpacket end='w'", false),
};

// closing part of all valid XMP trailers
constexpr auto xmpTrailerEnd = std::string_view("?>");

//! Write data into temp file, taking care of errors
void writeTemp(BasicIo& tempIo, const byte* data, size_t size) {
  if (size == 0)
    return;
  if (tempIo.write(data, size) != size) {
#ifndef SUPPRESS_WARNINGS
    EXV_WARNING << "Failed to write to temporary file.\n";
#endif
    throw Error(ErrorCode::kerImageWriteFailed);
  }
}

//! Write data into temp file, taking care of errors
void writeTemp(BasicIo& tempIo, const std::string& data) {
  writeTemp(tempIo, reinterpret_cast<const byte*>(data.data()), data.size());
}

//! Get the current write position of temp file, taking care of errors
uint32_t posTemp(const BasicIo& tempIo) {
  const size_t pos = tempIo.tell();
  enforce(pos <= std::numeric_limits<uint32_t>::max(), ErrorCode::kerImageWriteFailed);
  return static_cast<uint32_t>(pos);
}

//! Check whether a string contains only white space characters
bool onlyWhitespaces(const std::string& s) {
  // According to the DSC 3.0 specification, 4.4 Parsing Rules,
  // only spaces and tabs are considered to be white space characters.
  return s.find_first_not_of(" \t") == std::string::npos;
}

//! Read the next line of a buffer, allow for changing line ending style
size_t readLine(std::string& line, const byte* data, size_t startPos, size_t size) {
  line.clear();
  size_t pos = startPos;
  // step through line
  while (pos < size && data[pos] != '\r' && data[pos] != '\n') {
    line += data[pos];
    pos++;
  }
  // skip line ending, if present
  if (pos >= size)
    return pos;
  pos++;
  if (pos >= size)
    return pos;
  if (data[pos - 1] == '\r' && data[pos] == '\n')
    pos++;
  return pos;
}

//! Read the previous line of a buffer, allow for changing line ending style
size_t readPrevLine(std::string& line, const byte* data, size_t startPos, size_t size) {
  line.clear();
  size_t pos = startPos;
  if (pos > size)
    return pos;
  // skip line ending of previous line, if present
  if (pos <= 0)
    return pos;
  if (data[pos - 1] == '\r' || data[pos - 1] == '\n') {
    pos--;
    if (pos <= 0)
      return pos;
    if (data[pos - 1] == '\r' && data[pos] == '\n') {
      pos--;
      if (pos <= 0)
        return pos;
    }
  }
  // step through previous line
  while (pos >= 1 && data[pos - 1] != '\r' && data[pos - 1] != '\n') {
    pos--;
    line += data[pos];
  }
  std::reverse(line.begin(), line.end());
  return pos;
}

//! Find an XMP block
void findXmp(size_t& xmpPos, size_t& xmpSize, const byte* data, size_t startPos, size_t size, bool write) {
  // search for valid XMP header
  xmpSize = 0;
  for (xmpPos = startPos; xmpPos < size; xmpPos++) {
    if (data[xmpPos] != '\x00' && data[xmpPos] != '<')
      continue;
    for (auto&& header : xmpHeaders) {
      if (xmpPos + header.size() > size)
        continue;
      if (memcmp(data + xmpPos, header.data(), header.size()) != 0)
        continue;
#ifdef DEBUG
      EXV_DEBUG << "findXmp: Found XMP header at position: " << xmpPos << "\n";
#endif

      // search for valid XMP trailer
      for (size_t trailerPos = xmpPos + header.size(); trailerPos < size; trailerPos++) {
        if (data[xmpPos] != '\x00' && data[xmpPos] != '<')
          continue;
        for (const auto& [trailer, readOnly] : xmpTrailers) {
          if (trailerPos + trailer.size() > size)
            continue;
          if (memcmp(data + trailerPos, trailer.data(), trailer.size()) != 0)
            continue;
#ifdef DEBUG
          EXV_DEBUG << "findXmp: Found XMP trailer at position: " << trailerPos << "\n";
#endif

          if (readOnly) {
#ifndef SUPPRESS_WARNINGS
            EXV_WARNING << "Unable to handle read-only XMP metadata yet. Please provide your "
                           "sample EPS file to the Exiv2 project: http://dev.exiv2.org/projects/exiv2\n";
#endif
            throw Error(write ? ErrorCode::kerImageWriteFailed : ErrorCode::kerFailedToReadImageData);
          }

          // search for end of XMP trailer
          for (size_t trailerEndPos = trailerPos + trailer.size(); trailerEndPos + xmpTrailerEnd.size() <= size;
               trailerEndPos++) {
            if (memcmp(data + trailerEndPos, xmpTrailerEnd.data(), xmpTrailerEnd.size()) == 0) {
              xmpSize = (trailerEndPos + xmpTrailerEnd.size()) - xmpPos;
              return;
            }
          }
#ifndef SUPPRESS_WARNINGS
          EXV_WARNING << "Found XMP header but incomplete XMP trailer.\n";
#endif
          throw Error(write ? ErrorCode::kerImageWriteFailed : ErrorCode::kerFailedToReadImageData);
        }
      }
#ifndef SUPPRESS_WARNINGS
      EXV_WARNING << "Found XMP header but no XMP trailer.\n";
#endif
      throw Error(write ? ErrorCode::kerImageWriteFailed : ErrorCode::kerFailedToReadImageData);
    }
  }
}

//! Unified implementation of reading and writing EPS metadata
void readWriteEpsMetadata(BasicIo& io, std::string& xmpPacket, NativePreviewList& nativePreviews, bool write) {
  // open input file
  if (io.open() != 0) {
    throw Error(ErrorCode::kerDataSourceOpenFailed, io.path(), strError());
  }
  IoCloser closer(io);

  // read from input file via memory map
  const byte* data = io.mmap();

  // default positions and sizes
  const size_t size = io.size();
  size_t posEps = 0;
  size_t posEndEps = size;
  uint32_t posWmf = 0;
  uint32_t sizeWmf = 0;
  uint32_t posTiff = 0;
  uint32_t sizeTiff = 0;

  // check for DOS EPS
  const bool dosEps =
      (size >= dosEpsSignature.size() && memcmp(data, dosEpsSignature.data(), dosEpsSignature.size()) == 0);
  if (dosEps) {
#ifdef DEBUG
    EXV_DEBUG << "readWriteEpsMetadata: Found DOS EPS signature\n";
#endif
    if (size < 30) {
#ifndef SUPPRESS_WARNINGS
      EXV_WARNING << "Premature end of file after DOS EPS signature.\n";
#endif
      throw Error(write ? ErrorCode::kerImageWriteFailed : ErrorCode::kerFailedToReadImageData);
    }
    posEps = getULong(data + 4, littleEndian);
    posEndEps = getULong(data + 8, littleEndian) + posEps;
    posWmf = getULong(data + 12, littleEndian);
    sizeWmf = getULong(data + 16, littleEndian);
    posTiff = getULong(data + 20, littleEndian);
    sizeTiff = getULong(data + 24, littleEndian);
#ifdef DEBUG
    EXV_DEBUG << "readWriteEpsMetadata: EPS section at position " << posEps << ", size " << (posEndEps - posEps)
              << "\n";
    EXV_DEBUG << "readWriteEpsMetadata: WMF section at position " << posWmf << ", size " << sizeWmf << "\n";
    EXV_DEBUG << "readWriteEpsMetadata: TIFF section at position " << posTiff << ", size " << sizeTiff << "\n";
#endif
    if (uint16_t checksum = getUShort(data + 28, littleEndian); checksum != 0xFFFF) {
#ifdef DEBUG
      EXV_DEBUG << "readWriteEpsMetadata: DOS EPS checksum is not FFFF\n";
#endif
    }
    if ((posWmf != 0 || sizeWmf != 0) && (posTiff != 0 || sizeTiff != 0)) {
#ifndef SUPPRESS_WARNINGS
      EXV_WARNING << "DOS EPS file has both WMF and TIFF section. Only one of those is allowed.\n";
#endif
      if (write)
        throw Error(ErrorCode::kerImageWriteFailed);
    }
    if (sizeWmf == 0 && sizeTiff == 0) {
#ifndef SUPPRESS_WARNINGS
      EXV_WARNING << "DOS EPS file has neither WMF nor TIFF section. Exactly one of those is required.\n";
#endif
      if (write)
        throw Error(ErrorCode::kerImageWriteFailed);
    }
    if (posEps < 30 || posEndEps > size) {
#ifndef SUPPRESS_WARNINGS
      EXV_WARNING << "DOS EPS file has invalid position (" << posEps << ") or size (" << (posEndEps - posEps)
                  << ") for EPS section.\n";
#endif
      throw Error(write ? ErrorCode::kerImageWriteFailed : ErrorCode::kerFailedToReadImageData);
    }
    if (sizeWmf != 0 && (posWmf < 30 || posWmf + sizeWmf > size)) {
#ifndef SUPPRESS_WARNINGS
      EXV_WARNING << "DOS EPS file has invalid position (" << posWmf << ") or size (" << sizeWmf
                  << ") for WMF section.\n";
#endif
      if (write)
        throw Error(ErrorCode::kerImageWriteFailed);
    }
    if (sizeTiff != 0 && (posTiff < 30 || posTiff + sizeTiff > size)) {
#ifndef SUPPRESS_WARNINGS
      EXV_WARNING << "DOS EPS file has invalid position (" << posTiff << ") or size (" << sizeTiff
                  << ") for TIFF section.\n";
#endif
      if (write)
        throw Error(ErrorCode::kerImageWriteFailed);
    }
  }

  // check first line
  std::string firstLine;
  const size_t posSecondLine = readLine(firstLine, data, posEps, posEndEps);
#ifdef DEBUG
  EXV_DEBUG << "readWriteEpsMetadata: First line: " << firstLine << "\n";
#endif
  if (!Exiv2::find(epsFirstLine, firstLine)) {
    throw Error(ErrorCode::kerNotAnImage, "EPS");
  }

  // determine line ending style of the first line
  if (posSecondLine >= posEndEps) {
#ifndef SUPPRESS_WARNINGS
    EXV_WARNING << "Premature end of file after first line.\n";
#endif
    throw Error(write ? ErrorCode::kerImageWriteFailed : ErrorCode::kerFailedToReadImageData);
  }
  const std::string lineEnding(reinterpret_cast<const char*>(data + posEps + firstLine.size()),
                               posSecondLine - (posEps + firstLine.size()));
#ifdef DEBUG
  if (lineEnding == "\n") {
    EXV_DEBUG << "readWriteEpsMetadata: Line ending style: Unix (LF)\n";
  } else if (lineEnding == "\r") {
    EXV_DEBUG << "readWriteEpsMetadata: Line ending style: Mac (CR)\n";
  } else if (lineEnding == "\r\n") {
    EXV_DEBUG << "readWriteEpsMetadata: Line ending style: DOS (CR LF)\n";
  } else {
    EXV_DEBUG << "readWriteEpsMetadata: Line ending style: (unknown)\n";
  }
#endif

  // scan comments
  size_t posLanguageLevel = posEndEps;
  size_t posContainsXmp = posEndEps;
  size_t posPages = posEndEps;
  size_t posExiv2Version = posEndEps;
  size_t posExiv2Website = posEndEps;
  size_t posEndComments = posEndEps;
  size_t posAi7Thumbnail = posEndEps;
  size_t posAi7ThumbnailEndData = posEndEps;
  size_t posBeginPhotoshop = posEndEps;
  size_t posEndPhotoshop = posEndEps;
  size_t posPage = posEndEps;
  size_t posBeginPageSetup = posEndEps;
  size_t posEndPageSetup = posEndEps;
  size_t posPageTrailer = posEndEps;
  size_t posEof = posEndEps;
  std::vector<std::pair<size_t, size_t>> removableEmbeddings;
  size_t depth = 0;
  const size_t maxDepth = std::numeric_limits<size_t>::max();
  bool illustrator8 = false;
  bool corelDraw = false;
  bool implicitPage = false;
  bool implicitPageSetup = false;
  bool implicitPageTrailer = false;
  bool inDefaultsPreviewPrologSetup = false;
  bool inRemovableEmbedding = false