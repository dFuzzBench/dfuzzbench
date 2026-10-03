// ================================================================================================= // Copyright 2002-2008 Adobe Systems Incorporated
// All Rights Reserved.
//
// NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.
//
// Adobe patent application tracking #P435, entitled 'Unique markers to simplify embedding data of
// one format in a file with a different format', inventors: Sean Parent, Greg Gilley.
// =================================================================================================

#include "XMP_Environment.h"	// ! This must be the first include!
#include "XMPCore_Impl.hpp"

#include "XMPMeta.hpp"

#include "XMP_Version.h"
#include "UnicodeInlines.incl_cpp"
#include "UnicodeConversions.hpp"

#if XMP_DebugBuild
	#include <iostream>
#endif

using namespace std;

#if XMP_WinBuild
#ifdef _MSC_VER
	#pragma warning ( disable : 4533 )	// initialization of '...' is skipped by 'goto ...'
	#pragma warning ( disable : 4702 )	// unreachable code
	#pragma warning ( disable : 4800 )	// forcing value to bool 'true' or 'false' (performance warning)
#endif
#endif

// *** Use the XMP_PropIsXyz (Schema, Simple, Struct, Array, ...) macros
// *** Add debug codegen checks, e.g. that typical masking operations really work
// *** Change all uses of strcmp and strncmp to XMP_LitMatch and XMP_LitNMatch


// =================================================================================================
// Local Types and Constants
// =========================

static const char * kPacketHeader  = "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>";
static const char * kPacketTrailer = "<?xpacket end=\"w\"?>";	// ! The w/r is at [size-4].

static const char * kRDF_XMPMetaStart = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"";
static const char * kRDF_XMPMetaEnd   = "</x:xmpmeta>";

static const char * kRDF_RDFStart     = "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">";
static const char * kRDF_RDFEnd       = "</rdf:RDF>";

static const char * kRDF_SchemaStart  = "<rdf:Description rdf:about=";
static const char * kRDF_SchemaEnd    = "</rdf:Description>";

static const char * kRDF_StructStart  = "<rdf:Description>";
static const char * kRDF_StructEnd    = "</rdf:Description>";

static const char * kRDF_BagStart     = "<rdf:Bag>";

static const char * kRDF_ItemStart    = "<rdf:li>";

static const char * kRDF_ValueStart   = "<rdf:value>";


// =================================================================================================
// Static Variables
// ================


// =================================================================================================
// Local Utilities
// ===============


// -------------------------------------------------------------------------------------------------
// EstimateRDFSize
// ---------------

//  *** Pull the strlen(kXyz) calls into constants.

static size_t
EstimateRDFSize ( const XMP_Node * currNode, XMP_Index indent, size_t indentLen )
{
	size_t outputLen = 2 * (indent*indentLen + currNode->name.size() + 4);	// The property element tags.
	
	if ( ! currNode->qualifiers.empty() ) {
		// This node has qualifiers, assume it is written using rdf:value and estimate the qualifiers.

		indent += 2;	// Everything else is indented inside the rdf:Description element.
		outputLen += 2 * ((indent-1)*indentLen + strlen(kRDF_StructStart) + 2);	// The rdf:Description tags.
		outputLen += 2 * (indent*indentLen + strlen(kRDF_ValueStart) + 2);		// The rdf:value tags.

