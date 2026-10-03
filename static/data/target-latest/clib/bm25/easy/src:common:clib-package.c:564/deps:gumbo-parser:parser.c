// Copyright 2010 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Author: jdtang@google.com (Jonathan Tang)

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "attribute.h"
#include "error.h"
#include "gumbo.h"
#include "insertion_mode.h"
#include "parser.h"
#include "tokenizer.h"
#include "tokenizer_states.h"
#include "utf8.h"
#include "util.h"
#include "vector.h"


#define AVOID_UNUSED_VARIABLE_WARNING(i) (void)(i)

#define GUMBO_STRING(literal) { literal, sizeof(literal) - 1 }
#define TERMINATOR { "", 0 }

static void* malloc_wrapper(void* unused, size_t size) {
  return malloc(size);
}

static void free_wrapper(void* unused, void* ptr) {
  return free(ptr);
}

const GumboOptions kGumboDefaultOptions = {
  &malloc_wrapper,
  &free_wrapper,
  NULL,
  8,
  false,
  -1,
};

static const GumboStringPiece kDoctypeHtml = GUMBO_STRING("html");
static const GumboStringPiece kPublicIdHtml4_0 = GUMBO_STRING(
    "-//W3C//DTD HTML 4.0//EN");
static const GumboStringPiece kPublicIdHtml4_01 = GUMBO_STRING(
    "-//W3C//DTD HTML 4.01//EN");
static const GumboStringPiece kPublicIdXhtml1_0 = GUMBO_STRING(
    "-//W3C//DTD XHTML 1.0 Strict//EN");
static const GumboStringPiece kPublicIdXhtml1_1 = GUMBO_STRING(
    "-//W3C//DTD XHTML 1.1//EN");
static const GumboStringPiece kSystemIdRecHtml4_0 = GUMBO_STRING(
    "http://www.w3.org/TR/REC-html40/strict.dtd");
static const GumboStringPiece kSystemIdHtml4 = GUMBO_STRING(
    "http://www.w3.org/TR/html4/strict.dtd");
static const GumboStringPiece kSystemIdXhtmlStrict1_1 = GUMBO_STRING(
    "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd");
static const GumboStringPiece kSystemIdXhtml1_1 = GUMBO_STRING(
    "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd");
static const GumboStringPiece kSystemIdLegacyCompat = GUMBO_STRING(
    "about:legacy-compat");

// The doctype arrays have an explicit terminator because we want to pass them
// to a helper function, and passing them as a pointer discards sizeof
// information.  The SVG arrays are used only by one-off functions, and so loops
// over them use sizeof directly instead of a terminator.

static const GumboStringPiece kQuirksModePublicIdPrefixes[] = {
  GUMBO_STRING("+//Silmaril//dtd html Pro v0r11 19970101//"),
  GUMBO_STRING("-//AdvaSoft Ltd//DTD HTML 3.0 asWedit + extensions//"),
  GUMBO_STRING("-//AS//DTD HTML 3.0 asWedit + extensions//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Level 1//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Level 2//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Strict Level 1//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Strict Level 2//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0 Strict//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.0//"),
  GUMBO_STRING("-//IETF//DTD HTML 2.1E//"),
  GUMBO_STRING("-//IETF//DTD HTML 3.0//"),
  GUMBO_STRING("-//IETF//DTD HTML 3.2 Final//"),
  GUMBO_STRING("-//IETF//DTD HTML 3.2//"),
  GUMBO_STRING("-//IETF//DTD HTML 3//"),
  GUMBO_STRING("-//IETF//DTD HTML Level 0//"),
  GUMBO_STRING("-//IETF//DTD HTML Level 1//"),
  GUMBO_STRING("-//IETF//DTD HTML Level 2//"),
  GUMBO_STRING("-//IETF//DTD HTML Level 3//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict Level 0//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict Level 1//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict Level 2//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict Level 3//"),
  GUMBO_STRING("-//IETF//DTD HTML Strict//"),
  GUMBO_STRING("-//IETF//DTD HTML//"),
  GUMBO_STRING("-//Metrius//DTD Metrius Presentational//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 2.0 HTML Strict//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 2.0 HTML//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 2.0 Tables//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 3.0 HTML Strict//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 3.0 HTML//"),
  GUMBO_STRING("-//Microsoft//DTD Internet Explorer 3.0 Tables//"),
  GUMBO_STRING("-//Netscape Comm. Corp.//DTD HTML//"),
  GUMBO_STRING("-//Netscape Comm. Corp.//DTD Strict HTML//"),
  GUMBO_STRING("-//O'Reilly and Associates//DTD HTML 2.0//"),
  GUMBO_STRING("-//O'Reilly and Associates//DTD HTML Extended 1.0//"),
  GUMBO_STRING("-//O'Reilly and Associates//DTD HTML Extended Relaxed 1.0//"),
  GUMBO_STRING("-//SoftQuad Software//DTD HoTMetaL PRO 6.0::19990601::)"
      "extensions to HTML 4.0//"),
  GUMBO_STRING("-//SoftQuad//DTD HoTMetaL PRO 4.0::19971010::"
      "extensions to HTML 4.0//"),
  GUMBO_STRING("-//Spyglass//DTD HTML 2.0 Extended//"),
  GUMBO_STRING("-//SQ//DTD HTML 2.0 HoTMetaL + extensions//"),
  GUMBO_STRING("-//Sun Microsystems Corp.//DTD HotJava HTML//"),
  GUMBO_STRING("-//Sun Microsystems Corp.//DTD HotJava Strict HTML//"),
  GUMBO_STRING("-//W3C//DTD HTML 3 1995-03-24//"),
  GUMBO_STRING("-//W3C//DTD HTML 3.2 Draft//"),
  GUMBO_STRING("-//W3C//DTD HTML 3.2 Final//"),
  GUMBO_STRING("-//W3C//DTD HTML 3.2//"),
  GUMBO_STRING("-//W3C//DTD HTML 3.2S Draft//"),
  GUMBO_STRING("-//W3C//DTD HTML 4.0 Frameset//"),
  GUMBO_STRING("-//W3C//DTD HTML 4.0 Transitional//"),
  GUMBO_STRING("-//W3C//DTD HTML Experimental 19960712//"),
  GUMBO_STRING("-//W3C//DTD HTML Experimental 970421//"),
  GUMBO_STRING("-//W3C//DTD W3 HTML//"),
  GUMBO_STRING("-//W3O//DTD W3 HTML 3.0//"),
  GUMBO_STRING("-//WebTechs//DTD Mozilla HTML 2.0//"),
  GUMBO_STRING("-//WebTechs//DTD Mozilla HTML//"),
  TERMINATOR
};

static const GumboStringPiece kQuirksModePublicIdExactMatches[] = {
  GUMBO_STRING("-//W3O//DTD W3 HTML Strict 3.0//EN//"),
  GUMBO_STRING("-/W3C/DTD HTML 4.0 Transitional/EN"),
  GUMBO_STRING("HTML"),
  TERMINATOR
};

static const GumboStringPiece kQuirksModeSystemIdExactMatches[] = {
  GUMBO_STRING("http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd"),
  TERMINATOR
};

static const GumboStringPiece kLimitedQuirksPublicIdPrefixes[] = {
  GUMBO_STRING("-//W3C//DTD XHTML 1.0 Frameset//"),
  GUMBO_STRING("-//W3C//DTD XHTML 1.0 Transitional//"),
  TERMINATOR
};

static const GumboStringPiece kLimitedQuirksRequiresSystemIdPublicIdPrefixes[] = {
  GUMBO_STRING("-//W3C//DTD HTML 4.01 Frameset//"),
  GUMBO_STRING("-//W3C//DTD HTML 4.01 Transitional//"),
  TERMINATOR
};

// Indexed by GumboNamespaceEnum; keep in sync with that.
static const char* kLegalXmlns[] = {
  "http://www.w3.org/1999/xhtml",
  "http://www.w3.org/2000/svg",
  "http://www.w3.org/1998/Math/MathML"
};

typedef struct _ReplacementEntry {
  const GumboStringPiece from;
  const GumboStringPiece to;
} ReplacementEntry;

#define REPLACEMENT_ENTRY(from, to) \
    { GUMBO_STRING(from), GUMBO_STRING(to) }

// Static data for SVG attribute replacements.
// http://www.whatwg.org/specs/web-apps/current-work/multipage/tree-construction.html#adjust-svg-attributes
static const ReplacementEntry kSvgAttributeReplacements[] = {
  REPLACEMENT_ENTRY("attributename", "attributeName"),
  REPLACEMENT_ENTRY("attributetype", "attributeType"),
  REPLACEMENT_ENTRY("basefrequency", "baseFrequency"),
  REPLACEMENT_ENTRY("baseprofile", "baseProfile"),
  REPLACEMENT_ENTRY("calcmode", "calcMode"),
  REPLACEMENT_ENTRY("clippathunits", "clipPathUnits"),
  REPLACEMENT_ENTRY("contentscripttype", "contentScriptType"),
  REPLACEMENT_ENTRY("contentstyletype", "contentStyleType"),
  REPLACEMENT_ENTRY("diffuseconstant", "diffuseConstant"),
  REPLACEMENT_ENTRY("edgemode", "edgeMode"),
  REPLACEMENT_ENTRY("externalresourcesrequired", "externalResourcesRequired"),
  REPLACEMENT_ENTRY("filterres", "filterRes"),
  REPLACEMENT_ENTRY("filterunits", "filterUnits"),
  REPLACEMENT_ENTRY("glyphref", "glyphRef"),
  REPLACEMENT_ENTRY("gradienttransform", "gradientTransform"),
  REPLACEMENT_ENTRY("gradientunits", "gradientUnits"),
  REPLACEMENT_ENTRY("kernelmatrix", "kernelMatrix"),
  REPLACEMENT_ENTRY("kernelunitlength", "kernelUnitLength"),
  REPLACEMENT_ENTRY("keypoints", "keyPoints"),
  REPLACEMENT_ENTRY("keysplines", "keySplines"),
  REPLACEMENT_ENTRY("keytimes", "keyTimes"),
  REPLACEMENT_ENTRY("lengthadjust", "lengthAdjust"),
  REPLACEMENT_ENTRY("limitingconeangle", "limitingConeAngle"),
  REPLACEMENT_ENTRY("markerheight", "markerHeight"),
  REPLACEMENT_ENTRY("markerunits", "markerUnits"),
  REPLACEMENT_ENTRY("markerwidth", "markerWidth"),
  REPLACEMENT_ENTRY("maskcontentunits", "maskContentUnits"),
  REPLACEMENT_ENTRY("maskunits", "maskUnits"),
  REPLACEMENT_ENTRY("numoctaves", "numOctaves"),
  REPLACEMENT_ENTRY("pathlength", "pathLength"),
  REPLACEMENT_ENTRY("patterncontentunits", "patternContentUnits"),
  REPLACEMENT_ENTRY("patterntransform", "patternTransform"),
  REPLACEMENT_ENTRY("patternunits", "patternUnits"),
  REPLACEMENT_ENTRY("pointsatx", "pointsAtX"),
  REPLACEMENT_ENTRY("pointsaty", "pointsAtY"),
  REPLACEMENT_ENTRY("pointsatz", "pointsAtZ"),
  REPLACEMENT_ENTRY("preservealpha", "preserveAlpha"),
  REPLACEMENT_ENTRY("preserveaspectratio", "preserveAspectRatio"),
  REPLACEMENT_ENTRY("primitiveunits", "primitiveUnits"),
  REPLACEMENT_ENTRY("refx", "refX"),
  REPLACEMENT_ENTRY("refy", "refY"),
  REPLACEMENT_ENTRY("repeatcount", "repeatCount"),
  REPLACEMENT_ENTRY("repeatdur", "repeatDur"),
  REPLACEMENT_ENTRY("requiredextensions", "requiredExtensions"),
  REPLACEMENT_ENTRY("requiredfeatures", "requiredFeatures"),
  REPLACEMENT_ENTRY("specularconstant", "specularConstant"),
  REPLACEMENT_ENTRY("specularexponent", "specularExponent"),
  REPLACEMENT_ENTRY("spreadmethod", "spreadMethod"),
  REPLACEMENT_ENTRY("startoffset", "startOffset"),
  REPLACEMENT_ENTRY("stddeviation", "stdDeviation"),
  REPLACEMENT_ENTRY("stitchtiles", "stitchTiles"),
  REPLACEMENT_ENTRY("surfacescale", "surfaceScale"),
  REPLACEMENT_ENTRY("systemlanguage", "systemLanguage"),
  REPLACEMENT_ENTRY("tablevalues", "tableValues"),
  REPLACEMENT_ENTRY("targetx", "targetX"),
  REPLACEMENT_ENTRY("targety", "targetY"),
  REPLACEMENT_ENTRY("textlength", "textLength"),
  REPLACEMENT_ENTRY("viewbox", "viewBox"),
  REPLACEMENT_ENTRY("viewtarget", "viewTarget"),
  REPLACEMENT_ENTRY("xchannelselector", "xChannelSelector"),
  REPLACEMENT_ENTRY("ychannelselector", "yChannelSelector"),
  REPLACEMENT_ENTRY("zoomandpan", "zoomAndPan"),
};

static const ReplacementEntry kSvgTagReplacements[] = {
  REPLACEMENT_ENTRY("altglyph", "altGlyph"),
  REPLACEMENT_ENTRY("altglyphdef", "altGlyphDef"),
  REPLACEMENT_ENTRY("altglyphitem", "altGlyphItem"),
  REPLACEMENT_ENTRY("animatecolor", "animateColor"),
  REPLACEMENT_ENTRY("animatemotion", "animateMotion"),
  REPLACEMENT_ENTRY("animatetransform", "animateTransform"),
  REPLACEMENT_ENTRY("clippath", "clipPath"),
  REPLACEMENT_ENTRY("feblend", "feBlend"),
  REPLACEMENT_ENTRY("fecolormatrix", "feColorMatrix"),
  REPLACEMENT_ENTRY("fecomponenttransfer", "feComponentTransfer"),
  REPLACEMENT_ENTRY("fecomposite", "feComposite"),
  REPLACEMENT_ENTRY("feconvolvematrix", "feConvolveMatrix"),
  REPLACEMENT_ENTRY("fediffuselighting", "feDiffuseLighting"),
  REPLACEMENT_ENTRY("fedisplacementmap", "feDisplacementMap"),
  REPLACEMENT_ENTRY("fedistantlight", "feDistantLight"),
  REPLACEMENT_ENTRY("feflood", "feFlood"),
  REPLACEMENT_ENTRY("fefunca", "feFuncA"),
  REPLACEMENT_ENTRY("fefuncb", "feFuncB"),
  REPLACEMENT_ENTRY("fefuncg", "feFuncG"),
  REPLACEMENT_ENTRY("fefuncr", "feFuncR"),
  REPLACEMENT_ENTRY("fegaussianblur", "feGaussianBlur"),
  REPLACEMENT_ENTRY("feimage", "feImage"),
  REPLACEMENT_ENTRY("femerge", "feMerge"),
  REPLACEMENT_ENTRY("femergenode", "feMergeNode"),
  REPLACEMENT_ENTRY("femorphology", "feMorphology"),
  REPLACEMENT_ENTRY("feoffset", "feOffset"),
  REPLACEMENT_ENTRY("fepointlight", "fePointLight"),
  REPLACEMENT_ENTRY("fespecularlighting", "feSpecularLighting"),
  REPLACEMENT_ENTRY("fespotlight", "feSpotLight"),
  REPLACEMENT_ENTRY("fetile", "feTile"),
  REPLACEMENT_ENTRY("feturbulence", "feTurbulence"),
  REPLACEMENT_ENTRY("foreignobject", "foreignObject"),
  REPLACEMENT_ENTRY("glyphref", "glyphRef"),
  REPLACEMENT_ENTRY("lineargradient", "linearGradient"),
  REPLACEMENT_ENTRY("radialgradient", "radialGradient"),
  REPLACEMENT_ENTRY("textpath", "textPath"),
};

typedef struct _Namespaced