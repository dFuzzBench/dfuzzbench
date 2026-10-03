// SPDX-License-Identifier: GPL-2.0-or-later
// included header files
#include "asfvideo.hpp"

#include <cstring>
#include <iostream>
#include <sstream>

#include "basicio.hpp"
#include "config.h"
#include "enforce.hpp"
#include "error.hpp"
#include "futils.hpp"
#include "helper_functions.hpp"
#include "utils.hpp"
// *****************************************************************************
// class member definitions
namespace Exiv2 {

/*!
  Look-up list for ASF Type Video Files
  Associates the GUID with its Name(i.e. Human Readable Form)
  Tags have been differentiated into Various Categories.
  The categories have been listed above Groups
  see :
  - https://fr.wikipedia.org/wiki/Advanced_Systems_Format
  - https://exse.eyewated.com/fls/54b3ed95bbfb1a92.pdf
 */
/*
 * @class GUID_struct
 *
 * @brief A class to represent a globally unique identifier (GUID) structure
 *
 * This class represents a globally unique identifier (GUID) structure which is used to identify objects in a
 * distributed environment. A GUID is a unique identifier that is generated on a computer and can be used to
 * identify an object across different systems. The GUID structure is comprised of four 32-bit values and an
 * array of 8 bytes.
 *
 * @note The byte order of the GUID structure is in little endian.
 *
 * @see https://en.wikipedia.org/wiki/Globally_unique_identifier
 *
 */

bool AsfVideo::GUIDTag::operator==(const AsfVideo::GUIDTag& other) const {
  return data1_ == other.data1_ && data2_ == other.data2_ && data3_ == other.data3_ && data4_ == other.data4_;
}

AsfVideo::GUIDTag::GUIDTag(const uint8_t* bytes) {
  std::memcpy(&data1_, bytes, DWORD);
  std::memcpy(&data2_, bytes + DWORD, WORD);
  std::memcpy(&data3_, bytes + DWORD + WORD, WORD);
  std::copy(bytes + QWORD, bytes + 2 * QWORD, data4_.begin());
  if (isBigEndianPlatform()) {
    data1_ = byteSwap(data1_, true);
    data2_ = byteSwap(data2_, true);
    data3_ = byteSwap(data3_, true);
  }
}

std::string AsfVideo::GUIDTag::to_string() {
  // Convert each field of the GUID structure to a string
  std::stringstream ss;
  ss << std::hex << std::setw(8) << std::setfill('0') << data1_ << "-";
  ss << std::hex << std::setw(4) << std::setfill('0') << data2_ << "-";
  ss << std::hex << std::setw(4) << std::setfill('0') << data3_ << "-";

  for (size_t i = 0; i < 8; i++) {
    if (i == 2) {
      ss << "-";
    }
    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data4_[i]);
  }

  // Concatenate all strings into a single string
  // Convert the string to uppercase
  // Example of output 399595EC-8667-4E2D-8FDB-98814CE76C1E
  return Internal::upper(ss.str());
}

bool AsfVideo::GUIDTag::operator<(const GUIDTag& other) const {
  if (data1_ != other.data1_)
    return data1_ < other.data1_;
  if (data2_ != other.data2_)
    return data2_ < other.data2_;
  if (data3_ != other.data3_)
    return data3_ < other.data3_;
  return std::lexicographical_compare(data4_.begin(), data4_.end(), other.data4_.begin(), other.data4_.end());
}

constexpr AsfVideo::GUIDTag Header(0x75B22630, 0x668E, 0x11CF, {0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C});

const std::map<AsfVideo::GUIDTag, std::string> GUIDReferenceTags = {
    //!< Top-level ASF object GUIDS
    {Header, "Header"},
    {{0x75B22636, 0x668E, 0x11CF, {0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C}}, "Data"},
    {{0x33000890, 0xE5B1, 0x11CF, {0x89, 0xF4, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xCB}}, "Simple_Index"},
    {{0xD6E229D3, 0x35DA, 0x11D1, {0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE}}, "Index"},
    {{0xFEB103F8, 0x12AD, 0x4C64, {0x84, 0x0F, 0x2A, 0x1D, 0x2F, 0x7A, 0xD4, 0x8C}}, "Media_Index"},
    {{0x3CB73FD0, 0x0C4A, 0x4803, {0x95, 0x3D, 0xED, 0xF7, 0xB6, 0x22, 0x8F, 0x0C}}, "Timecode_Index"},

    //!< Header Object GUIDs
    {{0x8CABDCA1, 0xA947, 0x11CF, {0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65}}, "File_Properties"},
    {{0xB7DC0791, 0xA9B7, 0x11CF, {0x8E, 0xE6, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65}}, "Stream_Properties"},
    {{0x5FBF03B5, 0xA92E, 0x11CF, {0x8E, 0xE3, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65}}, "Header_Extension"},
    {{0x86D15240, 0x311D, 0x11D0, {0xA3, 0xA4, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6}}, "Codec_List"},
    {{0x1EFB1A30, 0x0B62, 0x11D0, {0xA3, 0x9B, 0x00, 0xA0, 0xC9, 0x03, 0x48, 0xF6}}, "Script_Command"},
    {{0xF487CD01, 0xA951, 0x11CF, {0x8E, 0xE6, 0x00, 0xC0, 0x00, 0xC2, 0x05, 0x36}}, "Marker"},
    {{0xD6E229DC, 0x35DA, 0x11D1, {0x90, 0x34, 0x00, 0xA0, 0xC9, 0x03, 0x49, 0xBE}}, "Bitrate_Mutual_Exclusion"},
    {{0x75B22635, 0x668E, 0x11CF, {0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C}}, "Error_Correction"},
    {{0x75B22633, 0x668E, 0x11CF, {0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C}}, "Content_Description"},
    {{0xD2D0A440, 0xE307, 0x11D2, {0x97, 0xF0, 0x00, 0xA0, 0xC9, 0x5E, 0xA8, 0x50}}, "Extended_Content_Description"},
    {{0x2211B3FA, 0xBD23, 0x11D2, {0xB4, 0xB7, 0x00, 0xA0, 0xC9, 0x55, 0xFC, 0x6E}}, "Content_Branding"},
    {{0x7BF875CE, 0x468D, 0x11D1, {0x8D, 0x82, 0x00, 0x60, 0x97, 0xC9, 0xA2, 0xB2}}, "Stream_Bitrate_Properties"},
    {{0x2211B3FB, 0xBD23, 0x11D2, {0xB4, 0xB7, 0x00, 0xA0, 