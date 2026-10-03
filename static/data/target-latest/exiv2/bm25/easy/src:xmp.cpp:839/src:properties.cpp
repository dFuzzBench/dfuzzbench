// SPDX-License-Identifier: GPL-2.0-or-later

// included header files
#include "properties.hpp"

#include "error.hpp"
#include "i18n.h"  // NLS support.
#include "metadatum.hpp"
#include "tags_int.hpp"
#include "types.hpp"
#include "value.hpp"
#include "xmp_exiv2.hpp"

#include <iostream>

namespace {
//! Struct used in the lookup table for pretty print functions
struct XmpPrintInfo {
  //! Comparison operator for key
  bool operator==(const std::string& key) const {
    return key == key_;
  }

  const char* key_;           //!< XMP key
  Exiv2::PrintFct printFct_;  //!< Print function
};

}  // namespace

// *****************************************************************************
// class member definitions
namespace Exiv2 {
using namespace Internal;

//! @cond IGNORE
extern const XmpPropertyInfo xmpDcInfo[];
extern const XmpPropertyInfo xmpDigikamInfo[];
extern const XmpPropertyInfo xmpKipiInfo[];
extern const XmpPropertyInfo xmpXmpInfo[];
extern const XmpPropertyInfo xmpXmpRightsInfo[];
extern const XmpPropertyInfo xmpXmpMMInfo[];
extern const XmpPropertyInfo xmpXmpBJInfo[];
extern const XmpPropertyInfo xmpXmpTPgInfo[];
extern const XmpPropertyInfo xmpXmpDMInfo[];
extern const XmpPropertyInfo xmpMicrosoftInfo[];
extern const XmpPropertyInfo xmpPdfInfo[];
extern const XmpPropertyInfo xmpPhotoshopInfo[];
extern const XmpPropertyInfo xmpCrsInfo[];
extern const XmpPropertyInfo xmpCrssInfo[];
extern const XmpPropertyInfo xmpTiffInfo[];
extern const XmpPropertyInfo xmpExifInfo[];
extern const XmpPropertyInfo xmpExifEXInfo[];
extern const XmpPropertyInfo xmpAuxInfo[];
extern const XmpPropertyInfo xmpIptcInfo[];
extern const XmpPropertyInfo xmpIptcExtInfo[];
extern const XmpPropertyInfo xmpPlusInfo[];
extern const XmpPropertyInfo xmpMediaProInfo[];
extern const XmpPropertyInfo xmpExpressionMediaInfo[];
extern const XmpPropertyInfo xmpMicrosoftPhotoInfo[];
extern const XmpPropertyInfo xmpMicrosoftPhotoRegionInfoInfo[];
extern const XmpPropertyInfo xmpMicrosoftPhotoRegionInfo[];
extern const XmpPropertyInfo xmpMWGRegionsInfo[];
extern const XmpPropertyInfo xmpMWGKeywordInfo[];
extern const XmpPropertyInfo xmpVideoInfo[];
extern const XmpPropertyInfo xmpAudioInfo[];
extern const XmpPropertyInfo xmpDwCInfo[];
extern const XmpPropertyInfo xmpDctermsInfo[];
extern const XmpPropertyInfo xmpLrInfo[];
extern const XmpPropertyInfo xmpAcdseeInfo[];
extern const XmpPropertyInfo xmpGPanoInfo[];

constexpr XmpNsInfo xmpNsInfo[] = {
    // Schemas   -   NOTE: Schemas which the XMP-SDK doesn't know must be registered in XmpParser::initialize -
    // Todo: Automate this
    {"http://purl.org/dc/elements/1.1/", "dc", xmpDcInfo, N_("Dublin Core schema")},
    {"http://www.digikam.org/ns/1.0/", "digiKam", xmpDigikamInfo, N_("digiKam Photo Management schema")},
    {"http://www.digikam.org/ns/kipi/1.0/", "kipi", xmpKipiInfo, N_("KDE Image Program Interface schema")},
    {"http://ns.adobe.com/xap/1.0/", "xmp", xmpXmpInfo, N_("XMP Basic schema")},
    {"http://ns.adobe.com/xap/1.0/rights/", "xmpRights", xmpXmpRightsInfo, N_("XMP Rights Management schema")},
    {"http://ns.adobe.com/xap/1.0/mm/", "xmpMM", xmpXmpMMInfo, N_("XMP Media Management schema")},
    {"http://ns.adobe.com/xap/1.0/bj/", "xmpBJ", xmpXmpBJInfo, N_("XMP Basic Job Ticket schema")},
    {"http://ns.adobe.com/xap/1.0/t/pg/", "xmpTPg", xmpXmpTPgInfo, N_("XMP Paged-Text schema")},
    {"http://ns.adobe.com/xmp/1.0/DynamicMedia/", "xmpDM", xmpXmpDMInfo, N_("XMP Dynamic Media schema")},
    {"http://ns.microsoft.com/photo/1.0/", "MicrosoftPhoto", xmpMicrosoftInfo, N_("Microsoft Photo schema")},
    {"http://ns.adobe.com/lightroom/1.0/", "lr", xmpLrInfo, N_("Adobe Lightroom schema")},
    {"http://ns.adobe.com/pdf/1.3/", "pdf", xmpPdfInfo, N_("Adobe PDF schema")},
    {"http://ns.adobe.com/photoshop/1.0/", "photoshop", xmpPhotoshopInfo, N_("Adobe photoshop schema")},
    {"http://ns.adobe.com/camera-raw-settings/1.0/", "crs", xmpCrsInfo, N_("Camera Raw schema")},
    {"http://ns.adobe.com/camera-raw-saved-settings/1.0/", "crss", xmpCrssInfo, N_("Camera Raw Saved Settings")},
    {"http://ns.adobe.com/tiff/1.0/", "tiff", xmpTiffInfo, N_("Exif Schema for TIFF Properties")},
    {"http://ns.adobe.com/exif/1.0/", "exif", xmpExifInfo, N_("Exif schema for Exif-specific Properties")},
    {"http://cipa.jp/exif/1.0/", "exifEX", xmpExifEXInfo, N_("Exif 2.3 metadata for XMP")},
    {"http://ns.adobe.com/exif/1.0/aux/", "aux", xmpAuxInfo, N_("Exif schema for Additional Exif Properties")},
    {"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "iptc", xmpIptcInfo,
     N_("IPTC Core schema")},  // NOTE: 'Iptc4xmpCore' is just too long, so make 'iptc'
    {"http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/", "Iptc4xmpCore", xmpIptcInfo,
     N_("IPTC Core schema")},  // the default prefix. But provide the official one too.
    {"http://iptc.org/std/Iptc4xmpExt/2008-02-29/", "iptcExt", xmpIptcExtInfo,
     N_("IPTC Extension schema")},  // NOTE: It really should be 'Iptc4xmpExt' but following
    {"http://iptc.org/std/Iptc4xmpExt/2008-02-29/", "Iptc4xmpExt", xmpIptcExtInfo,
     N_("IPTC Extension schema")},  // example above, 'iptcExt' is the default, Iptc4xmpExt works too.
    {"http://ns.useplus.org/ldf/xmp/1.0/", "plus", xmpPlusInfo, N_("PLUS License Data Format schema")},
    {"http://ns.iview-multimedia.com/mediapro/1.0/", "mediapro", xmpMediaProInfo, N_("iView Media Pro schema")},
    {"http://ns.microsoft.com/expressionmedia/1.0/", "expressionmedia", xmpExpressionMediaInfo,
     N_("Expression Media schema")},
    {"http://ns.microsoft.com/photo/1.2/", "MP", xmpMicrosoftPhotoInfo, N_("Microsoft Photo 1.2 schema")},
    {"http://ns.microsoft.com/photo/1.2/t/RegionInfo#", "MPRI", xmpMicrosoftPhotoRegionInfoInfo,
     N_("Microsoft Photo RegionInfo schema")},
    {"http://ns.microsoft.com/photo/1.2/t/Region#", "MPReg", xmpMicrosoftPhotoRegionInfo,
     N_("Microsoft Photo Region schema")},
    {"http://www.metadataworkinggroup.com/schemas/regions/", "mwg-rs", xmpMWGRegionsInfo,
     N_("Metadata Working Group Regions schema")},
    {"http://www.metadataworkinggroup.com/schemas/keywords/", "mwg-kw", xmpMWGKeywordInfo,
     N_("Metadata Working Group Keywords schema")},
    {"http://www.video/", "video", xmpVideoInfo, N_("XMP Extended Video schema")},
    {"http://www.audio/", "audio", xmpAudioInfo, N_("XMP Extended Audio schema")},
    {"http://rs.tdwg.org/dwc/index.htm", "dwc", xmpDwCInfo, N_("XMP Darwin Core schema")},
    {"http://purl.org/dc/terms/", "dcterms", xmpDctermsInfo,
     N_("Qualified Dublin Core schema")},  // Note: used as properties under dwc:record
    {"http://ns.acdsee.com/iptc/1.0/", "acdsee", xmpAcdseeInfo, N_("ACDSee XMP schema")},
    {"http://ns.google.com/photos/1.0/panorama/", "GPano", xmpGPanoInfo, N_("Google Photo Sphere XMP schema")},

    // Structures
    {"http://ns.adobe.com/xap/1.0/g/", "xmpG", nullptr, N_("Colorant structure")},
    {"http://ns.adobe.com/xap/1.0/g/img/", "xmpGImg", nullptr, N_("Thumbnail structure")},
    {"http://ns.adobe.com/xap/1.0/sType/Dimensions#", "stDim", nullptr, N_("Dimensions structure")},
    {"http://ns.adobe.com/xap/1.0/sType/Font#", "stFnt", nullptr, N_("Font structure")},
    {"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#", "stEvt", nullptr, N_("Resource Event structure")},
    {"http://ns.adobe.com/xap/1.0/sType/ResourceRef#", "stRef", nullptr, N_("ResourceRef structure")},
    {"http://ns.adobe.com/xap/1.0/sType/Version#", "stVer", nullptr, N_("Version structure")},
    {"http://ns.adobe.com/xap/1.0/sType/Job#", "stJob", nullptr, N_("Basic Job/Workflow structure")},
    {"http://ns.adobe.com/xmp/sType/Area#", "stArea", nullptr, N_("Area structure")},

    // Qualifiers
    {"http://ns.adobe.com/xmp/Identifier/qual/1.0/", "xmpidq", nullptr, N_("Qualifier for xmp:Identifier")},
};

const XmpPropertyInfo xmpDcInfo[] = {
    {"contributor", N_("Contributor"), "bag ProperName", xmpBag, xmpExternal,
     N_("Contributors to the resource (other than the authors).")},
    {"coverage", N_("Coverage"), "Text", xmpText, xmpExternal,
     N_("The spatial or temporal topic of the resource, the spatial applicability of the "
        "resource, or the jurisdiction under which the resource is relevant.")},
    {"creator", N_("Creator"), "seq ProperName", xmpSeq, xmpExternal,
     N_("The authors of the resource (listed in order of precedence, if significant).")},
    {"date", N_("Date"), "seq Date", xmpSeq, xmpExternal,
     N_("Date(s) that something interesting happened to the resource.")},
    {"description", N_("Description"), "Lang Alt", langAlt, xmpExternal,
     N_("A textual description of the content of the resource. Multiple values may be "
        "present for different languages.")},
    {"format", N_("Format"), "MIMEType", xmpText, xmpInternal,
     N_("The file format used when saving the resource. Tools and applications should set "
        "this property to the save format of the data. It may include appropriate qualifiers.")},
    {"identifier", N_("Identifier"), "Text", xmpText, xmpExternal,
     N_("Unique identifier of the resource. Recommended best practice is to identify the "
        "resource by means of a string conforming to a formal identification system.")},
    {"language", N_("Language"), "bag Locale", xmpBag, xmpInternal,
     N_("An unordered array specifying the languages used in the resource.")},
    {"publisher", N_("Publisher"), "bag ProperName", xmpBag, xmpExternal,
     N_("An entity responsible for making the resource available. Examples of a Publisher "
        "include a person, an organization, or a service. Typically, the name of a Publisher "
        "should be used to indicate the entity.")},
    {"relation", N_("Relation"), "bag Text", xmpBag, xmpInternal,
     N_("Relationships to other documents. Recommended best practice is to identify the "
        "related resource by means of a string conforming to a formal identification system.")},
    {"rights", N_("Rights"), "Lang Alt", langAlt, xmpExternal,
     N_("Informal rights statement, selected by language. Typically, rights information "
        "includes a statement about various property rights associated with the resource, "
        "including intellectual property rights.")},
    {"source", N_("Source"), "Text", xmpText, xmpExternal,
     N_("Unique identifier of the work from which this resource was derived.")},
    {"subject", N_("Subject"), "bag Text", xmpBag, xmpExternal,
     N_("An unordered array of descriptive phrases or keywords that specify the topic of the "
        "content of the resource.")},
    {"title", N_("Title"), "Lang Alt", langAlt, xmpExternal,
     N_("The title of the document, or the name given to the resource. Typically, it will be "
        "a name by which the resource is formally known.")},
    {"type", N_("Type"), "bag open Choice", xmpBag, xmpExternal,
     N_("A document type; for example, novel, poem, or working paper.")},
    // End of list marker
    {nullptr, nullptr, nullptr, invalidTypeId, xmpInternal, nullptr},
};

const XmpPropertyInfo xmpDigikamInfo[] = {
    {"TagsList", N_("Tags List"), "seq Text", xmpSeq, xmpExternal,
     N_("The list of complete tags path as string. The path hierarchy is separated by '/' character (ex.: "
        "\"City/Paris/Monument/Eiffel Tower\".")},
    {"CaptionsAuthorNames", N_("Captions Author Names"), "Lang Alt", langAlt, xmpExternal,
     N_("The list of all captions author names for each language alternative captions set in standard XMP tags.")},
    {"CaptionsDateTimeStamps", N_("Captions Date Time Stamps"), "Lang Alt", langAlt, xmpExternal,
     N_("The list of all captions date time stamps for each language alternative captions set in standard XMP "
        "tags.")},
    {"ImageHistory", N_("Image History"), "Text", xmpText, xmpExternal,
     N_("An XML based content to list all action processed on this image with image editor (as crop, rotate, color "
        "corrections, adjustments, etc.).")},
    {"LensCorrectionSettings", N_("Lens Correction Settings"), "Text", xmpText, xmpExternal,
     N_("The list of Lens Correction tools settings used to fix lens distortion. This include Batch Queue Manager "
        "and Image editor tools based on LensFun library.")},
    {"ColorLabel", N_("Color Label"), "Text", xmpText, xmpExternal,
     N_("The color label assigned to this item. Possible values are \"0\": no label; \"1\": Red; \"2\": Orange; "
        "\"3\": Yellow; \"4\": Green; \"5\": Blue; \"6\": Magenta; \"7\": Gray; \"8\": Black; \"9\": White.")},
    {"PickLabel", N_("Pick Label"), "Text", xmpText, xmpExternal,
     N_("The pick label assigned to this item. Possible values are \"0\": no label; \"1\": item rejected; \"2\": "
        "item in pending validation; \"3\": item accepted.")},
    {"Preview", N_("JPEG preview"), "Text", xmpText, xmpExternal,
     N_("Reduced size JPEG preview image encoded as base64 for a fast screen rendering.")},
    // End of list marker
    {nullptr, nullptr, nullptr, invalidTypeId, xmpInternal, nullptr},
};

const XmpPropertyInfo xmpKipiInfo[] = {
    {"PanoramaInputFiles", N_("Panorama Input Files"), "Text", xmpText, xmpExternal,
     N_("The list of files processed with Hugin program through Panorama tool.")},
    {"EnfuseInputFiles", N_("Enfuse Input Files"), "Text", xmpText, xmpExternal,
     N_("The list of files processed with Enfuse program through ExpoBlending tool.")},
    {"EnfuseSettings", N_("Enfuse Settings"), "Text", xmpText, xmpExternal,
     N_("The list of Enfuse settings used to blend image stack with ExpoBlending tool.")},
    {"picasawebGPhotoId", N_("PicasaWeb Item ID"), "Text", xmpText, xmpExternal,
     N_("Item ID from PicasaWeb web service.")},
    {"yandexGPhotoId", N_("Yandex Fotki Item ID"), "Text", xmpText, xmpExternal,
     N_("Item ID from Yandex Fotki web service.")},
    // End of list marker
    {nullptr, nullptr, nullptr, invalidTypeId, xmpInternal, nullptr},
};

const XmpPropertyInfo xmpXmpInfo[] = {
    {"Advisory", N_("Advisory"), "bag XPath", xmpBag, xmpExternal,
     N_("An unordered array specifying properties that were edited outside the authoring "
        "application. Each item should contain a single namespace and XPath separated by "
        "one ASCII space (U+0020).")},
    {"BaseURL", N_("Base URL"), "URL", xmpText, xmpInternal,
     N_("The base URL for relative URLs in the document content. If this document contains "
        "Internet links, and those links are relative, they are relative to this base URL. "
        "This property provides a standard way for embedded relative URLs to be interpreted "
        "by tools. Web authoring tools should set the value based on their notion of where "
        "URLs will be interpreted.")},
    {"CreateDate", N_("Create Date"), "Date", xmpText, xmpExternal,
     N_("The date and time the resource was originally created.")},
    {"CreatorTool", N_("Creator Tool"), "AgentName", xmpText, xmpInternal,
     N_("The name of the first known tool used to create the resource. If history is "
        "present in the metadata, this value should be equivalent to that of "
        "xmpMM:History's softwareAgent property.")},
    {"Identifier", N_("Identifier"), "bag Text", xmpBag, xmpExternal,
     N_("An unordered array of text strings that unambiguously identify the resource within "
        "a given context. An array item may be qualified with xmpidq:Scheme to denote the "
        "formal identification system to which that identifier conforms. Note: The "
        "dc:identifier property is not used because it lacks a defined scheme qualifier and "
        "has been defined in the XMP Specification as a simple (single-valued) property.")},
    {"Label", N_("Label"), "Text", xmpText, xmpExternal,
     N_("A word or short phrase that identifies a document as a member of a user-defined "
        "collection. Used to organize documents in a file browser.")},
    {"MetadataDate", N_("Metadata Date"), "Date", xmpText, xmpInternal,
     N_("The date and time that any metadata for this resource was last changed. It should "
        "be the same as or more recent than xmp:ModifyDate.")},
    {"ModifyDate", N_("Modify Date"), "Date", xmpText, xmpInternal,
     N_("The date and time the resource was last modified. Note: The value of this property "
        "is not necessarily the same as the file's system modification date because it is "
        "set before the file is saved.")},
    {"Nickname", N_("Nickname"), "Text", xmpText, xmpExternal, N_("A short informal name for the resource.")},
    {"Rating", N_("Rating"), "Closed Choice of Real", xmpText, xmpExternal,
     N_("A number that indicates a document's status relative to other documents, "
        "used to organize documents in a file browser. Values are user-defined within an "
        "application-defined range.")},
    {"Thumbnails", N_("Thumbnails"), "alt Thumbnail", xmpText, xmpInternal,
     N_("An alternative array of thumbnail images for a file, which can differ in "
        "characteristics such as size or image encoding.")},
    // End of list marker
    {nullptr, nullptr, nullptr, invalidTypeId, xmpInternal, nullptr},
};

const XmpPropertyInfo xmpXmpRightsInfo[] = {
    {"Certificate", N_("Certificate"), "URL", xmpText, xmpExternal, N_("Online rights management certificate.")},
    {"Marked", N_("Marked"), "Boolean", xmpText, xmpExternal, N_("Indicates that this is a rights-managed resource.")},
    {"Owner", N_("Owner"), "bag ProperName", xmpBag, xmpExternal,
     N_("An unordered array specifying the legal owner(s) of a resource.")},
    {"UsageTerms", N_("Usage Terms"), "Lang Alt", langAlt, xmpExternal,
     N_("Text instructions on how a resource can be legally used.")},
    {"WebStatement", N_("Web Statement"), "URL", xmpText, xmpExternal,
     N_("The location of a web page describing the owner and/or rights statement for this resource.")},
    // End of list marker
    {nullptr, nullptr, nullptr, invalidTypeId, xmpInternal, nullptr},
};

const XmpPropertyInfo xmpXmpMMInfo[] = {
    {"DerivedFrom", N_("Derived From"), "ResourceRef", xmpText, xmpInternal,
     N_("A reference to the original document from which this one is derived. It is a "
        "minimal reference; missing components can be assumed to be unchanged. For example, "
        "a new version might only need to specify the instance ID and version number of the "
        "previous version, or a rendition might only need to specify the instance ID and "
        "rendition class of the original.")},
    {"DocumentID", N_("Document ID"), "URI", xmpText, xmpInternal,
     N_("The common identifier for all versions and renditions of a document. It should be "
        "based on a UUID; see Document and Instance IDs below.")},
    {"History", N_("History"), "seq ResourceEvent", xmpSeq, xmpInternal,
     N_("An ordered array of high-level user actions that resulted in this resource. It is "
        "intended to give human readers a general indication of the steps taken to make the "
        "changes from the previous version to this one. The list should be at an abstract "
        "level; it is not intended to be an exhaustive keystroke or other detailed history.")},
    {"Ingredients", N_("Ingredients"), "bag ResourceRef", xmpBag, xmpInternal,
     N_("References to resources that were incorporated, by inclusion or reference, into this resource.")},
    {"InstanceID", N_("Instance ID"), "URI", xmpText, xmpInternal,
     N_("An identifier for a specific incarnation of a document, updated each time a file "
        "is saved. It should be based on a UUID; see Document and Instance IDs below.")},
    {"ManagedFrom", N_("Managed From"), "ResourceRef", xmpText, xmpInternal,
     N_("A reference to the document as it was prior to becoming managed. It is set when a "
        "managed document is introduced to an asset management system that does not "
        "currently own it. It may or may not include references to different management systems.")},
    {"Manager", N_("Manager"), "AgentName", xmpText, xmpInternal,
     N_("The name of the asset management system that manages this resource. Along with "
        "xmpMM: ManagerVariant, it tells applications which asset management system to "
        "contact concerning this document.")},
    {"ManageTo", N_("Manage To"), "URI", xmpText, xmpInternal,
     N_("A URI identifying the managed resource to the asset management system; the presence "
        "of this property is the formal indication that this resource is managed. The form "
        "and content of this URI is private to the asset management system.")},
    {"ManageUI", N_("Manage UI"), "URI", xmpText, xmpInternal,
     N_("A URI that can be used to access information about the managed resource through a "
        "web browser. It might require a custom browser plug-in.")},
    {"ManagerVariant", N_("Manager Variant"), "Text", xmpText, xmpInternal,
     N_("Specifies a particular variant of the asset management system. The format of this "
        "property is private to the specific asset management system.")},
    {"OriginalDocumentID", N_("Original Document ID"), "URI", xmpText, xmpInternal,
     N_("Refer to Part 1, Data Model, Serialization, and Core "
        "Properties, for definition.")},
    {"Pantry", N_("Pantry"), "bag struct", xmpText, xmpInternal,
     N_("Each array item has a structure value with a potentially "
        "unique set of fields, containing extracted XMP from a "
        "component. Each field is a property from the XMP of a "
        "contained resource component, with all substructure "
        "preserved. "
        "Each pantry entry shall contain an xmpMM:InstanceID. "
        "Only one copy of the pantry entry for any given "
        "xmpMM:InstanceID shall be retained in the pantry. "
        "Nested pantry items shall be removed from the individual "
        "pantry item and promoted to the top level of the pantry.")},
    {"RenditionClass", N_("Rendition Class"), "RenditionClass", xmpText, xmpInternal,
     N_("The rendition class name for this resource. This property should be absent or set "
        "to default for a document version that is not a derived rendition.")},
    {"RenditionParams", N_("Rendition Params"), "Text", xmpText, xmpInternal,
     N_("Can be used to provide additional rendition parameters that are too complex or "
        "verbose to encode in xmpMM: RenditionClass.")},
    {"VersionID", N_("Version ID"), "Text", xmpText, xmpInternal,
     N_("The document version identifier for this resource. Each version of a document gets "
        "a new identifier, usually simply by incrementing integers 1, 2, 3 . . . and so on. "
        "Media management systems can have other conventions or support branching which "
        "requires a more complex scheme.")},
    {"Versions", N_("Versions"), "seq Version", xmpText, xmpInternal,
     N_("The version history associated with this resource. Entry [1] is the oldest known "
        "version for this document, entry [last()] is the most recent version. Typically, a "
        "media management system would fill in the version information in the metadata on "
        "check-in. It is not guaranteed that a complete history  versions from the first to "
        "this one will be present in the xmpMM:Versions property. Interior version information "
        "can be compressed or eliminated and the version history can be truncated at some point.")},
    {"LastURL", N_("Last URL"), "URL", xmpText, xmpInternal, N_("Deprecated for privacy protection.")},
    {"RenditionOf", N_("Rendition Of"), "ResourceRef", xmpText, xmpInternal,
     N_("Deprecated in favor of xmpMM:DerivedFrom. A reference to the document of which this is "
        "a rendition.")},
    {"SaveID", N_("Save ID"), "Integer", xmpText, xmpInternal,
     N_("Deprecated. Previously used only to support the xmpMM:LastURL property.")},
    // End of list marker
    {nullptr, nullptr, nullptr, invalidTypeId, xmpInternal, nullptr},
};

const XmpPropertyInfo xmpXmpBJInfo[] = {
    {"JobRef", N_("Job Reference"), "bag Job", xmpText, xmpExternal,
     N_("References an external job management file for a job process in which the document is being used. Use of "
        "job "
        "names is under user control. Typical use would be to identify all documents that are part of a particular "
        "job or contract. "
        "There are multiple values because there can be more than one job using a particular document at any time, "
        "and it can "
        "also be useful to keep historical information about what jobs a document was part of previously.")},
    // End of list marker
    {nullptr, nullptr, nullptr, invalidTypeId, xmpInternal, nullptr},
};

const XmpPropertyInfo xmpXmpTPgInfo[] = {
    {"MaxPageSize", N_("Maximum Page Size"), "Dimensions", xmpText, xmpInternal,
     N_("The size of the largest page in the document (including any in contained documents).")},
    {"NPages", N_("Number of Pages"), "Integer", xmpText, xmpInternal,
     N_("The number of pages in the document (including any in contained documents).")},
    {"Fonts", N_("Fonts"), "bag Font", xmpText, xmpInternal,
     N_("An unordered array of fonts that are used in the document (including any in contained documents).")},
    {"Colorants", N_("Colorants"), "seq Colorant", xmpText, xmpInternal,
     N_("An ordered array of colorants (swatches) that are used in the document (including any in contained "
        "documents).")},
    {"PlateNames", N_("Plate Names"), "seq Text", xmpSeq, xmpInternal,
     N_("An ordered array of plate names that are needed to print the document (including any in contained "
        "documents).")},
    // End of list marker
    {nullptr, nullptr, nullptr, invalidTypeId, xmpInternal, nullptr},
};

const XmpPropertyInfo xmpXmpDMInfo[] = {
    {"absPeakAudioFilePath", N_("Absolute Peak Audio File Path"), "URI", xmpText, xmpInternal,
     N_("The absolute path to the file's peak audio file. If empty, no peak file exists.")},
    {"album", N_("Album"), "Text", xmpText, xmpExternal, N_("The name of the album.")},
    {"altTapeName", N_("Alternative Tape Name"), "Text", xmpText, xmpExternal,
     N_("An alternative tape name, set via the project window or timecode dialog in Premiere. "
        "If an alternative name has been set and has not been reverted, that name is displayed.")},
    {"altTimecode", N_("Alternative Time code"), "Timecode", xmpText, xmpExternal,
     N_("A timecode set by the user. When specified, it is used instead of the startTimecode.")},
    {"artist", N_("Artist"), "Text", xmpText, xmpExternal, N_("The name of the artist or artists.")},
    {"audioModDate", N_("Audio Modified Date"), "Date", xmpText, xmpInternal,
     N_("(deprecated) The date and time when the audio was last modified.")},
    {"audioChannelType", N_("Audio Channel Type"), "closed Choice of Text", xmpText, xmpInternal,
     N_("The audio channel type. One of: Mono, Stereo, 5.1, 7.1, 16 Channel, Other.")},
    {"audioCompressor", N_("Audio Compressor"), "Text", xmpText, xmpInternal,
     N_("The audio compression used. For example, MP3.")},
    {"audioSampleRate", N_("Audio Sample Rate"), "Integer", xmpText, xmpInternal,
     N_("The audio sample rate. Can be any value, but commonly 32000, 44100, or 48000.")},
    {"audioSampleType", N_("Audio Sample Type"), "closed Choice of Text", xmpText, xmpInternal,
     N_("The audio sample type. One of: 8Int, 16Int, 24Int, 32Int, 32Float, Compressed, Packed, Other.")},
    {"beatSpliceParams", N_("Beat Splice Parameters"), "beatSpliceStretch", xmpText, xmpInternal,
     N_("Additional parameters for Beat Splice stretch mode.")},
    {"cameraAngle", N_("Camera Angle"), "open Choice of Text", xmpText, xmpExternal,
     N_("The orientation of the camera to the subject in a static shot, from a fixed set of industry standard "
        "terminology. Predefined values include:Low Angle, Eye Level, High Angle, Overhead Shot, Birds Eye Shot, "
        "Dutch Angle, POV, Over the Shoulder, Reaction Shot.")},
    {"cameraLabel", N_("Camera Label"), "Text", xmpText, xmpExternal,
     N_("A description of the camera used for a shoot. Can be any string, but is usually simply a number, for "
        "example \"1\", \"2\", or more explicitly \"Camera 1\".")},
    {"cameraModel", N_("Camera Model"), "Text", xmpText, xmpExternal,
     N_("The make and model of the camera used for a shoot.")},
    {"cameraMove", N_("Camera Move"), "open Choice of Text", xmpText, xmpExternal,
     N_("The movement of the camera during the shot, from a fixed set of industry standard terminology. Predefined "
        "values include: Aerial, Boom Up, Boom Down, Crane Up, Crane Down, Dolly In, Dolly Out, Pan Left, Pan "
        "Right, Pedestal Up, Pedestal Down, Tilt Up, Tilt Down, Tracking, Truck Left, Truck Right, Zoom In, Zoom "
        "Out.")},
    {"client", N_("Client"), "Text", xmpText, xmpExternal,
     N_("The client for the job of which this shot or take is a part.")},
    {"comment", N_("Comment"), "Text", xmpText, xmpExternal, N_("A user's comments.")},
    {"composer", N_("Composer"), "Text", xmpText, xmpExternal, N_("The composer's name.")},
    {"contributedMedia", N_("Contributed Media"), "bag Media", xmpBag, xmpInternal,
     N_("An unordered list of all media used to create this media.")},
    {"copyright", N_("Copyright"), "Text", xmpText, xmpExternal,
     N_("(Deprecated in favour of dc:rights.) The copyright information.")},
    {"director", N_("Director"), "Text", xmpText, xmpExternal, N_("The director of the scene.")},
    {"directorPhotography", N_("Director Photography"), "Text", xmpText, xmpExternal,
     N_("The director of photography for the scene.")},
    {"duration", N_("Duration"), "Time", xmpText, xmpInternal, N_("The duration of the media file.")},
    {"engineer", N_("Engineer"), "Text", xmpText, xmpExternal, N_("The engineer's name.")},
    {"fileDataRate", N_("File Data Rate"), "Rational", xmpText, xmpInternal,
     N_("The file data rate in megabytes per second. For example: \"36/10\" = 3.6 MB/sec")},
    {"genre", N_("Genre"), "Text", xmpText, xmpExternal, N_("The name of the genre.")},
    {"good", N_("Good"), "Boolean", xmpText, xmpExternal, N_("A checkbox for tracking whether a shot is a keeper.")},
    {"instrument", N_("Instrument"), "Text", xmpText, xmpExternal, N_("The musical instrument.")},
    {"introTime", N_("Intro Time"), "Time", xmpText, xmpInternal, N_("The duration of lead time for queuing music.")},
    {"key", N_("Key"), "closed Choice of Text", xmpText, xmpInternal,
     N_("The audio's musical key. One of: C, C#, D, D#, E, F, F#, G, G#, A, A#, B.")},
    {"logComment", N_("Log Comment"), "Text", xmpText, xmpExternal, N_("User's log comments.")},
    {"loop", N_("Loop"), "Boolean", xmpText, xmpInternal, N_("When true, the clip can be looped seamlessly.")},
    {"numberOfBeats", N_("Number Of Beats"), "Real", xmpText, xmpInternal, N_("The number of beats.")},
    {"markers", N_("Markers"), "seq Marker", xmpSeq, xmpInternal, N_("An ordered list of markers")},
    {"metadataModDate", N_("Metadata Modified Date"), "Date", xmpText, xmpInternal,
     N_("(deprecated) The date and time when the metadata was last modified.")},
    {"outCue", N_("Out Cue"), "Time", xmpText, xmpInternal, N_("The time at which to fade out.")},
    {"projectName", N_("Project Name"), "Text", xmpText, xmpExternal,
     N_("The name of the project of which this file is a part.")},
    {"projectRef", N_("Project Reference"), "ProjectLink", xmpText, xmpInternal,
     N_("A reference to the project that created this file.")},
    {"pullDown", N_("Pull Down"), "closed Choice of Text", xmpText, xmpInternal,
     N_("The sampling phase of film to be converted to video (pull-down). One of: "
        "WSSWW, SSWWW, SWWWS, WWWSS, WWSSW, WSSWW_24p, SSWWW_24p, SWWWS_24p, WWWSS_24p, WWSSW_24p.")},
    {"relativePeakAudioFilePath", N_("Relative Peak Audio File Path"), "URI", xmpText, xmpInternal,
     N_("The relative path to the file's peak audio file. If empty, no peak file exists.")},
    {"relativeTimestamp", N_("Relative Timestamp"), "Time", xmpText, xmpInternal,
     N_("The start time of the media inside the audio project.")},
    {"releaseDate", N_("Release Date"), "Date", xmpText, xmpExternal, N_("The date the title was released.")},
    {"resampleParams", N_("Resample Parameters"), "resampleStretch", xmpText, xmpInternal,
     N_("Additional parameters for Resample stretch mode.")},
    {"scaleType", N_("Scale Type"), "closed Choice of Text", xmpText, xmpInternal,
     N_("The musical scale used in the music. One of: Major, Minor, Both, Neither.")},
    {"scene", N_("Scene"), "Text", xmpText, xmpExternal, N_("The name of the scene.")},
    {"shotDate", N_("Shot Date"), "Date", xmpText, xmpExternal, N_("The date and time when the video was shot.")},
    {"shotDay", N_("Shot Day"), "Text", xmpText, xmpExternal,
     N_("The day in a multiday shoot. For example: \"Day 2\", \"Friday\".")},
    {"shotLocation", N_("Shot Location"), "Text", xmpText, xmpExternal,
     N_("The name of the location where the video was shot. For example: \"Oktoberfest, Munich Germany\" "
        "For more accurate positioning, use the EXIF GPS values.")},
    {"shotName", N_("Shot Name"), "Text", xmpText, xmpExternal, N_("The name of the shot or take.")},
    {"shotNumber", N_("Shot Number"), "Text", xmpText, xmpExternal,
     N_("The position of the shot in a script or production, relative to other shots. For example: 1, 2, 1a, 1b, "
        "1.1, 1.2.")},
    {"shotSize", N_("Shot Size"), "open Choice of Text", xmpText, xmpExternal,
     N_("The size or scale of the shot framing, from a fixed set of industry standard terminology. Predefined "
        "values include: "
        "ECU --extreme close-up, MCU -- medium close-up. CU -- close-up, MS -- medium shot, "
        "WS -- wide shot, MWS -- medium wide shot, EWS -- extreme wide shot.")},
    {"speakerPlacement", N_("Speaker Placement"), "Text", xmpText, xmpExternal,
     N_("A description of the speaker angles from center front in degrees. For example: "
        "\"Left = -30, Right = 30, Center = 0, LFE = 45, Left Surround = -110, Right Surround = 110\"")},
    {"startTimecode", N_("Start Time Code"), "Timecode", xmpText, xmpInternal,
     N_("The timecode of the first frame of video in the file, as obtained from the device control.")},
    {"stretchMode", N_("Stretch Mode"), "closed Choice of Text", xmpText, xmpInternal,
     N_("The audio stretch mode. One of: Fixed length, Time-Scale, Resample, Beat Splice, Hybrid.")},
    {"takeNumber", N_("Take Number"), "Integer", xmpText, xmpExternal,
     N_("A numeric value indicating the absolute number of a take.")},
    {"tapeName", N_("Tape Name"), "Text", xmpText, xmpExternal,
     N_("The name of the tape from which the clip was captured, as set during the capture process.")},
    {"tempo", N_("Tempo"), "Real", xmpText, xmpInternal, N_("The audio's tempo.")},
    {"timeScaleParams", N_("Time Scale Parameters"), "timeScaleStretch", xmpText, xmpInternal,
     N_("Additional parameters for Time-Scale stretch mode.")},
