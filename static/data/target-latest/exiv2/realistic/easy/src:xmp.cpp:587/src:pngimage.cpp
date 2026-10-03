// SPDX-License-Identifier: GPL-2.0-or-later

// included header files
#include "config.h"

#ifdef EXV_HAVE_LIBZ
#include <zlib.h>  // To uncompress IccProfiles

#include "basicio.hpp"
#include "enforce.hpp"
#include "error.hpp"
#include "futils.hpp"
#include "image.hpp"
#include "image_int.hpp"
#include "jpgimage.hpp"
#include "photoshop.hpp"
#include "pngchunk_int.hpp"
#include "pngimage.hpp"
#include "tiffimage.hpp"
#include "types.hpp"
#include "utils.hpp"

#include <array>
#include <iostream>

namespace {
// Signature from front of PNG file
constexpr unsigned char pngSignature[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
};

constexpr unsigned char pngBlank[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
    0x01, 0x73, 0x52, 0x47, 0x42, 0x00, 0xae, 0xce, 0x1c, 0xe9, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73,
    0x00, 0x00, 0x0b, 0x13, 0x00, 0x00, 0x0b, 0x13, 0x01, 0x00, 0x9a, 0x9c, 0x18, 0x00, 0x00, 0x00, 0x0c, 0x49,
    0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xff, 0xff, 0x3f, 0x00, 0x05, 0xfe, 0x02, 0xfe, 0xdc, 0xcc, 0x59,
    0xe7, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

const auto nullComp = reinterpret_cast<const Exiv2::byte*>("\0\0");
const auto typeExif = reinterpret_cast<const Exiv2::byte*>("eXIf");
const auto typeICCP = reinterpret_cast<const Exiv2::byte*>("iCCP");
bool compare(std::string_view str, const Exiv2::DataBuf& buf) {
  const auto minlen = std::min<size_t>(str.size(), buf.size());
  return buf.cmpBytes(0, str.data(), minlen) == 0;
}
}  // namespace

// *****************************************************************************
// class member definitions
namespace Exiv2 {
using namespace Internal;

PngImage::PngImage(BasicIo::UniquePtr io, bool create) :
    Image(ImageType::png, mdExif | mdIptc | mdXmp | mdComment, std::move(io)) {
  if (create && io_->open() == 0) {
#ifdef EXIV2_DEBUG_MESSAGES
    std::cerr << "Exiv2::PngImage:: Creating PNG image to memory\n";
#endif
    IoCloser closer(*io_);
    if (io_->write(pngBlank, sizeof(pngBlank)) != sizeof(pngBlank)) {
#ifdef EXIV2_DEBUG_MESSAGES
      std::cerr << "Exiv2::PngImage:: Failed to create PNG image on memory\n";
#endif
    }
  }
}

std::string PngImage::mimeType() const {
  return "image/png";
}

static bool zlibToDataBuf(const byte* bytes, uLongf length, DataBuf& result) {
  uLongf uncompressedLen = length;  // just a starting point
  int zlibResult = Z_BUF_ERROR;

  do {
    result.alloc(uncompressedLen);
    zlibResult = uncompress(result.data(), &uncompressedLen, bytes, length);
    // if result buffer is large than necessary, redo to fit perfectly.
    if (zlibResult == Z_OK && uncompressedLen < result.size()) {
      result.reset();

      result.alloc(uncompressedLen);
      zlibResult = uncompress(result.data(), &uncompressedLen, bytes, length);
    }
    if (zlibResult == Z_BUF_ERROR) {
      // the uncompressed buffer needs to be larger
      result.reset();

      // Sanity - never bigger than 16mb
      if (uncompressedLen > 16 * 1024 * 1024)
        zlibResult = Z_DATA_ERROR;
      else
        uncompressedLen *= 2;
    }
  } while (zlibResult == Z_BUF_ERROR);

  return zlibResult == Z_OK;
}

static bool zlibToCompressed(const byte* bytes, uLongf length, DataBuf& result) {
  uLongf compressedLen = length;  // just a starting point
  int zlibResult = Z_BUF_ERROR;

  do {
    result.alloc(compressedLen);
    zlibResult = compress(result.data(), &compressedLen, bytes, length);
    if (zlibResult == Z_BUF_ERROR) {
      // the compressedArray needs to be larger
      result.reset();
      compressedLen *= 2;
    } else {
      result.reset();
      result.alloc(compressedLen);
      zlibResult = compress(result.data(), &compressedLen, bytes, length);
    }
  } while (zlibResult == Z_BUF_ERROR);

  return zlibResult == Z_OK;
}

static bool tEXtToDataBuf(const byte* bytes, size_t length, DataBuf& result) {
  static std::array<int, 256> value;
  static bool bFirst = true;
  if (bFirst) {
    value.fill(0);
    for (int i = 0; i < 10; i++) {
      value['0' + i] = i + 1;
    }
    for (int i = 0; i < 6; i++) {
      value['a' + i] = i + 10 + 1;
      value['A' + i] = i + 10 + 1;
    }
    bFirst = false;
  }

  // calculate length and allocate result;
  // count: number of \n in the header
  size_t count = 0;
  // p points to the current position in the array bytes
  const byte* p = bytes;

  // header is '\nsomething\n number\n hex'
  // => increment p until it points to the byte after the last \n
  //    p must stay within bounds of the bytes array!
  while (count < 3 && 0 < length) {
    // length is later used for range checks of p => decrement it for each increment of p
    --length;
    if (*p++ == '\n') {
      count++;
    }
  }
  for (size_t i = 0; i < length; i++)
    if (value[p[i]])
      ++count;
  result.alloc((count + 1) / 2);

  // hex to binary
  count = 0;
  byte* r = result.data();
  int n = 0;  // nibble
  for (size_t i = 0; i < length; i++) {
    if (value[p[i]]) {
      int v = value[p[i]] - 1;
      if (++count % 2)
        n = v * 16;  // leading digit
      else
        *r++ = n + v;  // trailing
    }
  }
  return true;
}

static std::string::size_type findi(const std::string& str, const std::string& substr) {
  return str.find(substr);
}

void PngImage::printStructure(std::ostream& out, PrintStructureOption option, size_t depth) {
  if (io_->open() != 0) {
    throw Error(ErrorCode::kerDataSourceOpenFailed, io_->path(), strError());
  }
  if (!isPngType(*io_, true)) {
    throw Error(ErrorCode::kerNotAnImage, "PNG");
  }

  char chType[5];
  chType[0] = 0;
  chType[4] = 0;

  if (option == kpsBasic || option == kpsXMP || option == kpsIccProfile || option == kpsRecursive) {
    const auto xmpKey = upper("XML:com.adobe.xmp");
    const auto exifKey = upper("Raw profile type exif");
    const auto app1Key = upper("Raw profile type APP1");
    const auto iptcKey = upper("Raw profile type iptc");
    const auto softKey = upper("Software");
    const auto commKey = upper("Comment");
    const auto descKey = upper("Description");

    bool bPrint = option == kpsBasic || option == kpsRecursive;
    if (bPrint) {
      out << "STRUCTURE OF PNG FILE: " << io_->path() << '\n';
      out << " address | chunk |  length | data                           | checksum" << '\n';
    }

    const size_t imgSize = io_->size();
    DataBuf cheaderBuf(8);

    while (!io_->eof() && ::strcmp(chType, "IEND") != 0) {
      const size_t address = io_->tell();

      size_t bufRead = io_->read(cheaderBuf.data(), cheaderBuf.size());
      if (io_->error())
        throw Error(ErrorCode::kerFailedToReadImageData);
      if (bufRead != cheaderBuf.size())
        throw Error(ErrorCode::kerInputDataReadFailed);

      // Decode chunk data length.
