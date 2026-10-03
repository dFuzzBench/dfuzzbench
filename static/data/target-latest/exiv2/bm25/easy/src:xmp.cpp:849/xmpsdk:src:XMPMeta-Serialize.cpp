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

		for ( size_t qualNum = 0, qualLim = currNode->qualifiers.size(); qualNum < qualLim; ++qualNum ) {
			const XMP_Node * currQual = currNode->qualifiers[qualNum];
			outputLen += EstimateRDFSize ( currQual, indent, indentLen );
		}

	}
	
	if ( currNode->options & kXMP_PropValueIsStruct ) {
		indent += 1;
		outputLen += 2 * (indent*indentLen + strlen(kRDF_StructStart) + 2);	// The rdf:Description tags.
	} else if ( currNode->options & kXMP_PropValueIsArray ) {
		indent += 2;
		outputLen += 2 * ((indent-1)*indentLen + strlen(kRDF_BagStart) + 2);		// The rdf:Bag/Seq/Alt tags.
		outputLen += 2 * currNode->children.size() * (strlen(kRDF_ItemStart) + 2);	// The rdf:li tags, indent counted in children.
	} else if ( ! (currNode->options & kXMP_SchemaNode) ) {
		outputLen += currNode->value.size();	// This is a leaf value node.
	}

	for ( size_t childNum = 0, childLim = currNode->children.size(); childNum < childLim; ++childNum ) {
		const XMP_Node * currChild = currNode->children[childNum];
		outputLen += EstimateRDFSize ( currChild, indent+1, indentLen );
	}

	return outputLen;
	
}	// EstimateRDFSize


// -------------------------------------------------------------------------------------------------
// DeclareOneNamespace
// -------------------

static void
DeclareOneNamespace	( const XMP_VarString &	nsPrefix,
					  const XMP_VarString &	nsURI,
					  XMP_VarString	&		usedNS,		// ! A catenation of the prefixes with colons.
					  XMP_VarString &		outputStr,
					  XMP_StringPtr			newline,
					  XMP_StringPtr			indentStr,
					  XMP_Index				indent )
{
	size_t nsPos = usedNS.find ( nsPrefix );

	if ( nsPos == XMP_VarString::npos ) {
		
		outputStr += newline;
		for ( ; indent > 0; --indent ) outputStr += indentStr;
		outputStr += "xmlns:";
		outputStr += nsPrefix;
		outputStr[outputStr.size()-1] = '=';	// Change the colon to =.
		outputStr += '"';
		outputStr += nsURI;
		outputStr += '"';

		usedNS += nsPrefix;

	}

}	// DeclareOneNamespace


// -------------------------------------------------------------------------------------------------
// DeclareElemNamespace
// --------------------

static void
DeclareElemNamespace ( const XMP_VarString & elemName,
					   XMP_VarString &		 usedNS,
					   XMP_VarString &		 outputStr,
					   XMP_StringPtr		 newline,
					   XMP_StringPtr		 indentStr,
					   XMP_Index			 indent )
{
	size_t colonPos = elemName.find ( ':' );

	if ( colonPos != XMP_VarString::npos ) {
		XMP_VarString nsPrefix ( elemName.substr ( 0, colonPos+1 ) );
		XMP_StringMapPos prefixPos = sNamespacePrefixToURIMap->find ( nsPrefix );
		XMP_Enforce ( prefixPos != sNamespacePrefixToURIMap->end() );
		DeclareOneNamespace ( nsPrefix, prefixPos->second, usedNS, outputStr, newline, indentStr, indent );
	}

}	// DeclareElemNamespace


// -------------------------------------------------------------------------------------------------
// DeclareUsedNamespaces
// ---------------------

// ??? Should iterators be passed by reference to avoid temp copies?

static void
DeclareUsedNamespaces ( const XMP_Node * currNode,
						XMP_VarString &  usedNS,
						XMP_VarString &	 outputStr,
						XMP_StringPtr	 newline,
						XMP_StringPtr	 indentStr,
						XMP_Index		 indent )
{

	if ( currNode->options & kXMP_SchemaNode ) {
		// The schema node name is the URI, the value is the prefix.
		DeclareOneNamespace ( currNode->value, currNode->name, usedNS, outputStr, newline, indentStr, indent );
	} else if ( currNode->options & kXMP_PropValueIsStruct ) {
		for ( size_t fieldNum = 0, fieldLim = currNode->children.size(); fieldNum < fieldLim; ++fieldNum ) {
			const XMP_Node * currField = currNode->children[fieldNum];
			DeclareElemNamespace ( currField->name, usedNS, outputStr, newline, indentStr, indent );
		}
	}

	for ( size_t childNum = 0, childLim = currNode->children.size(); childNum < childLim; ++childNum ) {
		const XMP_Node * currChild = currNode->children[childNum];
		DeclareUsedNamespaces ( currChild, usedNS, outputStr, newline, indentStr, indent );
	}

	for ( size_t qualNum = 0, qualLim = currNode->qualifiers.size(); qualNum < qualLim; ++qualNum ) {
		const XMP_Node * currQual = currNode->qualifiers[qualNum];
		DeclareElemNamespace ( currQual->name, usedNS, outputStr, newline, indentStr, indent );
		DeclareUsedNamespaces ( currQual, usedNS, outputStr, newline, indentStr, indent );
	}
	
}	// DeclareUsedNamespaces

// -------------------------------------------------------------------------------------------------
// EmitRDFArrayTag
// ---------------

// ??? Should iterators be passed by reference to avoid temp copies?

enum {
	kIsStartTag = true,
	kIsEndTag   = false
};

static void
EmitRDFArrayTag	( XMP_OptionBits  arrayForm,
				  XMP_VarString & outputStr,
				  XMP_StringPtr	  newline,
				  XMP_StringPtr	  indentStr,
				  XMP_Index		  indent,
				  XMP_Index		  arraySize,
				  bool			  isStartTag )
{
	if ( (! isStartTag) && (arraySize == 0) ) return;
	
	for ( XMP_Index level = indent; level > 0; --level ) outputStr += indentStr;
	if ( isStartTag ) {
		outputStr += "<rdf:";
	} else {
		outputStr += "</rdf:";
	}

	if ( arrayForm & kXMP_PropArrayIsAlternate ) {
		outputStr += "Alt";
	} else if ( arrayForm & kXMP_PropArrayIsOrdered ) {
		outputStr += "Seq";
	} else {
		outputStr += "Bag";
	}
	
	if ( isStartTag && (arraySize == 0) ) outputStr += '/';
	outputStr += '>';
	outputStr += newline;

}	// EmitRDFArrayTag


// -------------------------------------------------------------------------------------------------
// AppendNodeValue
// ---------------
//
// Append a property or qualifier value to the output with appropriate XML escaping. The escaped
// characters for elements and attributes are '&', '<', '>', and ASCII controls (tab, LF, CR). In
// addition, '"' is escaped for attributes. For efficiency, this is done in a double loop. The outer
// loop makes sure the whole value is processed. The inner loop does a contiguous unescaped run
// followed by one escaped character (if we're not at the end).
//
// We depend on parsing and SetProperty logic to make sure there are no invalid ASCII controls in
// the XMP values. The XML spec only allows tab, LF, and CR. Others are not even allowed as
// numeric escape sequences.

enum {
	kForAttribute = true,
	kForElement   = false
};

static void
AppendNodeValue ( XMP_VarString & outputStr, const XMP_VarString & value, bool forAttribute )
{

	unsigned char * runStart = (unsigned char *) value.c_str();
	unsigned char * runLimit  = runStart + value.size();
	unsigned char * runEnd;
	unsigned char   ch=0;
	
	while