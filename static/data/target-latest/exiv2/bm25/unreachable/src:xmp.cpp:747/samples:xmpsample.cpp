// SPDX-License-Identifier: GPL-2.0-or-later
// Sample/test for high level XMP classes. See also addmoddel.cpp

#include <exiv2/exiv2.hpp>

#include <cassert>
#include <cmath>
#include <iostream>

bool isEqual(float a, float b) {
  double d = std::fabs(a - b);
  return d < 0.00001;
}

int main() try {
  Exiv2::XmpParser::initialize();
  ::atexit(Exiv2::XmpParser::terminate);

  // The XMP property container
  Exiv2::XmpData xmpData;

  // -------------------------------------------------------------------------
  // Teaser: Setting XMP properties doesn't get much easier than this:

  xmpData["Xmp.dc.source"] = "xmpsample.cpp";  // a simple text value
  xmpData["Xmp.dc.subject"] = "Palmtree";      // an array item
  xmpData["X