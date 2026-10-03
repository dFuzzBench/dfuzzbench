// SPDX-License-Identifier: GPL-2.0-or-later
// Sample program to read gpx files and update images with GPS tags

#include <expat.h>
#include <exiv2/exiv2.hpp>

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <iostream>

#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

#ifdef _WIN32
#include <windows.h>
#if _MSC_VER < 1400
#define strcpy_s(d, l, s) strcpy(d, s)
#define strcat_s(d, l, s) strcat(d, s)
#endif
#endif

#if !defined(_MSC_VER)
#include <dirent.h>
#include <sys/param.h>
#include <unistd.h>
#define stricmp strcasecmp
#endif

#ifndef _MAX_PATH
#define _MAX_PATH 1024
#endif

// prototypes
class Options;
int getFileType(const char* path, Options& options);
int getFileType(std::string& path, Options& options);

std::string getExifTime(time_t t);
time_t parseTime(const char*, bool bAdjust = false);
int timeZoneAdjust();

// Command-line parser
class Options {
 public:
  bool verbose{false};
  bool help{false};
  bool version{false};
  bool dst{false};
  bool dryrun{false};
  bool ascii{false};

  Options() = default;
  virtual ~Options() = default;
};

enum {
  resultOK = 0,
  resultSyntaxError,
  resultSelectFailed,
};

// keyword indices
enum {
  kwHELP = 0,
  kwVERSION,
  kwDST,
  kwDRYRUN,
  kwASCII,
  kwVERBOSE,
  kwADJUST,
  kwTZ,
  kwDELTA,
  kwMAX,                   // manages keyword array
  kwNEEDVALUE,             // bogus keywords for error reporting
  kwSYNTAX,                // -- ditto --
  kwNOVALUE = -kwVERBOSE,  // keywords <= kwNOVALUE are flags (no value needed)
};

// file types supported
enum {
  typeUnknown = 0,
  typeDirectory = 1,
  typeImage = 2,
  typeXML = 3,
  typeFile = 4,
  typeDoc = 5,
  typeCode = 6,
  typeMax = 7
};

// forward declaration
class Position;

// globals
using TimeDict_t = std::map<time_t, Position>;
using TimeDict_i = std::map<time_t, Position>::iterator;
using strings_t = std::vector<std::string>;
const char* gDeg = nullptr;  // string "°" or "deg"
TimeDict_t gTimeDict;
strings_t gFiles;

// Position (from gpx file)
class Position {
 public:
  Position(time_t time, double lat, double lon, double ele) : time_(time), lon_(lon), lat_(lat), ele_(ele) {
  }

  Position() = default;
  virtual ~Position() = default;

  //  instance methods
  [[nodiscard]] bool good() const {
    return time_ || lon_ || lat_ || ele_;
  }
  std::string getTimeString() {
    if (times_.empty())
      times_ = getExifTime(time_);
    return times_;
  }
  [[nodiscard]] time_t getTime() const {
    return time_;
  }
  [[nodiscard]] std::string toString() const;

  //  getters/setters
  [[nodiscard]] double lat() const {
    return lat_;
  }
  [[nodiscard]] double lon() const {
    return lon_;
  }
  [[nodiscard]] double ele() const {
    return ele_;
  }
  [[nodiscard]] int delta() const {
    return delta_;
  }
  void delta(int delta) {
    delta_ = delta;
  }

  //  data
 private:
  time_t time_{0};
  double lon_{0.0};
  double lat_{0.0};
  double ele_{0.0};
  std::string times_;
  int delta_{0};

  // public static data
 public:
  static int adjust_;
  static int tz_;
  static int dst_;
  static time_t deltaMax_;

  // public static member functions

  static int Adjust() {
    return Position::adjust_ + Position::tz_ + Position::dst_;
  }
  static int tz() {
    return tz_;
  }
  static int dst() {
    return dst_;
  }
  static int adjust() {
    return adjust_;
  }

  static std::string toExifString(double d, bool bRational, bool bLat);
  static std::string toExifString(double d);
  static std::string toExifTimeStamp(std::string& t);
};

std::string Position::toExifTimeStamp(std::string& t) {
  char result[200];
  const char* arg = t.c_str();
  int HH = 0;
  int mm = 0;
  int SS1 = 0;
  if (strstr(arg, ":") || strstr(arg, "-")) {
    int YY = 0, MM = 0, DD = 0;
    char a = 0, b = 0, c = 0, d = 0, e = 0;
    sscanf(arg, "%d%c%d%c%d%c%d%c%d%c%d", &YY, &a, &MM, &b, &DD, &c, &HH, &d, &mm, &e, &SS1);
  }
  snprintf(result, sizeof(result), "%d/1 %d/1 %d/1", HH, mm, SS1);
  return result;
}

std::string Position::toExifString(double d) {
  char result[200];
  d *= 100;
  snprintf(result, sizeof