// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef VALUE_HPP_
#define VALUE_HPP_

// *****************************************************************************
#include "exiv2lib_export.h"

// included header files
#include "types.hpp"

// + standard includes
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <memory>

// *****************************************************************************
// namespace extensions
namespace Exiv2 {
// *****************************************************************************
// class definitions

/*!
  @brief Common interface for all types of values used with metadata.

  The interface provides a uniform way to access values independent of
  their actual C++ type for simple tasks like reading the values from a
  string or data buffer.  For other tasks, like modifying values you may
  need to downcast it to a specific subclass to access its interface.
 */
class EXIV2API Value {
 public:
  //! Shortcut for a %Value auto pointer.
  using UniquePtr = std::unique_ptr<Value>;

  //! @name Creators
  //@{
  //! Constructor, taking a type id to initialize the base class with
  explicit Value(TypeId typeId);
  //! Virtual destructor.
  virtual ~Value() = default;
  //@}

  //! @name Manipulators
  //@{

  /// @brief Read the value from a character buffer.
  /// @param buf Pointer to the data buffer to read from
  /// @param len Number of bytes in the data buffer
  /// @param byteOrder Applicable byte order (little or big endian).
  /// @return 0 if successful.
  virtual int read(const byte* buf, size_t len, ByteOrder byteOrder) = 0;

  /*!
    @brief Set the value from a string buffer. The format of the string
           corresponds to that of the write() method, i.e., a string
           obtained through the write() method can be read by this
           function.

    @param buf The string to read from.

    @return 0 if successful.
   */
  virtual int read(const std::string& buf) = 0;
  /*!
    @brief Set the data area, if the value has one by copying (cloning)
           the buffer pointed to by buf.

    Values may have a data area, which can contain additional
    information besides the actual value. This method is used to set such
    a data area.

    @param buf Pointer to the source data area
    @param len Size of the data area
    @return Return -1 if the value has no data area, else 0.
   */
  virtual int setDataArea(const byte* buf, size_t len);
  //@}

  //! @name Accessors
  //@{
  //! Return the type identifier (Exif data format type).
  TypeId typeId() const {
    return type_;
  }
  /*!
    @brief Return an auto-pointer to a copy of itself (deep copy).
           The caller owns this copy and the auto-pointer ensures that
           it will be deleted.
   */
  UniquePtr clone() const {
    return UniquePtr(clone_());
  }
  /*!
    @brief Write value to a data buffer.

    The user must ensure that the buffer has enough memory. Otherwise
    the call results in undefined behaviour.

    @param buf Data buffer to write to.
    @param byteOrder Applicable byte order (little or big endian).
    @return Number of bytes written.
  */
  virtual size_t copy(byte* buf, ByteOrder byteOrder) const = 0;
  //! Return the number of components of the value
  virtual size_t count() const = 0;
  //! Return the size of the value in bytes
  virtual size_t size() const = 0;
  /*!
    @brief Write the value to an output stream. You do not usually have
           to use this function; it is used for the implementation of
           the output operator for %Value,
           operator<<(std::ostream &os, const Value &value).
  */
  virtual std::ostream& write(std::ostream& os) const = 0;
  /*!
    @brief Return the value as a string. Implemented in terms of
           write(std::ostream& os) const of the concrete class.
   */
  std::string toString() const;
  /*!
    @brief Return the <EM>n</EM>-th component of the value as a string.
           The default implementation returns toString(). The behaviour
           of this method may be undefined if there is no <EM>n</EM>-th
           component.
   */
  virtual std::string toString(size_t n) const;
  /*!
    @brief Convert the <EM>n</EM>-th component of the value to an int64_t.
           The behaviour of this method may be undefined if there is no
           <EM>n</EM>-th component.

    @return The converted value.
   */
  virtual int64_t toInt64(size_t n = 0) const = 0;
  /*!
    @brief Convert the <EM>n</EM>-th component of the value to a float.
           The behaviour of this method may be undefined if there is no
           <EM>n</EM>-th component.

    @return The converted value.
   */
  virtual uint32_t toUint32(size_t n = 0) const = 0;
  /*!
    @brief Convert the <EM>n</EM>-th component of the value to a float.
           The behaviour of this method may be undefined if there is no
           <EM>n</EM>-th component.

    @return The converted value.
   */
  virtual float toFloat(size_t n = 0) const = 0;
  /*!
    @brief Convert the <EM>n</EM>-th component of the value to a Rational.
           The behaviour of this method may be undefined if there is no
           <EM>n</EM>-th component.

    @return The converted value.
   */
  virtual Rational toRational(size_t n = 0) const = 0;
  //! Return the size of the data area, 0 if there is none.
  virtual size_t sizeDataArea() const;
  /*!
    @brief Return a copy of the data area if the value has one. The
           caller owns this copy and DataBuf ensures that it will be
           deleted.

    Values may have a data area, which can contain additional
    information besides the actual value. This method is used to access
    such a data area.

    @return A DataBuf containing a copy of the data area or an empty
            DataBuf if the value does not have a data area assigned.
   */
  virtual DataBuf dataArea() const;
  /*!
    @brief Check the \em ok status indicator. After a to<Type> conversion,
           this indicator shows whether the conversion was successful.
   */
  bool ok() const {
    return ok_;
  }
  //@}

  /*!
    @brief A (simple) factory to create a Value type.

    The following Value subclasses are created depending on typeId:<BR><BR>
    <TABLE>
    <TR><TD><B>typeId</B></TD><TD><B>%Value subclass</B></TD></TR>
    <TR><TD><CODE>invalidTypeId</CODE></TD><TD>%DataValue(invalidTypeId)</TD></TR>
    <TR><TD><CODE>unsignedByte</CODE></TD><TD>%DataValue(unsignedByte)</TD></TR>
    <TR><TD><CODE>asciiString</CODE></TD><TD>%AsciiValue</TD></TR>
    <TR><TD><CODE>string</CODE></TD><TD>%StringValue</TD></TR>
    <TR><TD><CODE>unsignedShort</CODE></TD><TD>%ValueType &lt; uint16_t &gt;</TD></TR>
    <TR><TD><CODE>unsignedLong</CODE></TD><TD>%ValueType &lt; uint32_t &gt;</TD></TR>
    <TR><TD><CODE>unsignedRational</CODE></TD><TD>%ValueType &lt; URational &gt;</TD></TR>
    <TR><TD><CODE>invalid6</CODE></TD><TD>%DataValue(invalid6)</TD></TR>
    <TR><TD><CODE>undefined</CODE></TD><TD>%DataValue</TD></TR>
    <TR><TD><CODE>signedShort</CODE></TD><TD>%ValueType &lt; int16_t &gt;</TD></TR>
    <TR><TD><CODE>signedLong</CODE></TD><TD>%ValueType &lt; int32_t &gt;</TD></TR>
    <TR><TD><CODE>signedRational</CODE></TD><TD>%ValueType &lt; Rational &gt;</TD></TR>
    <TR><TD><CODE>tiffFloat</CODE></TD><TD>%ValueType &lt; float &gt;</TD></TR>
    <TR><TD><CODE>tiffDouble</CODE></TD><TD>%ValueType &lt; double &gt;</TD></TR>
    <TR><TD><CODE>tiffIfd</CODE></TD><TD>%ValueType &lt; uint32_t &gt;</TD></TR>
    <TR><TD><CODE>date</CODE></TD><TD>%DateValue</TD></TR>
    <TR><TD><CODE>time</CODE></TD><TD>%TimeValue</TD></TR>
    <TR><TD><CODE>comment</CODE></TD><TD>%CommentValue</TD></TR>
    <TR><TD><CODE>xmpText</CODE></TD><TD>%XmpTextValue</TD></TR>
    <TR><TD><CODE>xmpBag</CODE></TD><TD>%XmpArrayValue</TD></TR>
    <TR><TD><CODE>xmpSeq</CODE></TD><TD>%XmpArrayValue</TD></TR>
    <TR><TD><CODE>xmpAlt</CODE></TD><TD>%XmpArrayValue</TD></TR>
    <TR><TD><CODE>langAlt</CODE></TD><TD>%LangAltValue</TD></TR>
    <TR><TD><EM>default</EM></TD><TD>%DataValue(typeId)</TD></TR>
    </TABLE>

    @param typeId Type of the value.
    @return Auto-pointer to the newly created Value. The caller owns this
            copy and the auto-pointer ensures that it will be deleted.
   */
  static UniquePtr create(TypeId typeId);

 protected:
  Value(const Value&) = default;
  /*!
    @brief Assignment operator. Protected so that it can only be used
           by subclasses but not directly.
   */
  Value& operator=(const Value&) = default;
  // DATA
  mutable bool ok_{true};  //!< Indicates the status of the previous to<Type> conversion

 private:
  //! Internal virtual copy constructor.
  virtual Value* clone_() const = 0;
  // DATA
  TypeId type_;  //!< Type of the data
};

//! Output operator for Value types
inline std::ostream& operator<<(std::ostream& os, const Value& value) {
  return value.write(os);
}

//! %Value for an undefined data type.
class EXIV2API DataValue : public Value {
 public:
  //! Shortcut for a %DataValue auto pointer.
  using UniquePtr = std::unique_ptr<DataValue>;

  explicit DataValue(TypeId typeId = undefined);

  DataValue(const byte* buf, size_t len, ByteOrder byteOrder = invalidByteOrder, TypeId typeId = undefined);

  //! @name Manipulators
  //@{
  int read(const byte* buf, size_t len, ByteOrder byteOrder = invalidByteOrder) override;
  //! Set the data from a string of integer values (e.g., "0 1 2 3")
  int read(const std::string& buf) override;
  //@}

  //! @name Accessors
  //@{
  UniquePtr clone() const {
    return UniquePtr(clone_());
  }
  /*!
    @brief Write value to a character data buffer.

    @note The byte order is required by the interface but not used by this
          method, so just use the default.

    The user must ensure that the buffer has enough memory. Otherwise
    the call results in undefined behaviour.

    @param buf Data buffer to write to.
    @param byteOrder Byte order. Not needed.
    @return Number of characters written.
  */
  size_t copy(byte* buf, ByteOrder byteOrder = invalidByteOrder) const override;
  size_t count() const override;
  size_t size() const override;
  std::ostream& write(std::ostream& os) const override;
  /*!
    @brief Return the <EM>n</EM>-th component of the value as a string.
           The behaviour of this method may be undefined if there is no
           <EM>n</EM>-th component.
   */
  std::string toString(size_t n) const override;
  int64_t toInt64(size_t n = 0) const override;
  uint32_t toUint32(size_t n = 0) const override;
  float toFloat(size_t n = 0) const override;
  Rational toRational(size_t n = 0) const override;
  //@}

 private:
  //! Internal virtual copy constructor.
  DataValue* clone_() const override;

  //! Type used to store the data.
  using ValueType = std::vector<byte>;
  // DATA
  ValueType value_;  //!< Stores the data value

};  // class DataValue

/*!
  @brief Abstract base class for a string based %Value type.

  Uses a std::string to store the value and implements defaults for
  most operations.
 */
class EXIV2API StringValueBase : public Value {
  using Value::Value;

 public:
  //! Shortcut for a %StringValueBase auto pointer.
  using UniquePtr = std::unique_ptr<StringValueBase>;

  //! @name Creators
  //@{
  //! Constructor for subclasses
  StringValueBase(TypeId typeId, const std::string& buf);
  //@}

  //! @name Manipulators
  //@{
  //! Read the value from buf. This default implementation uses buf as it is.
  int read(const std::string& buf) override;
  int read(const byte* buf, size_t len, ByteOrder byteOrder = invalidByteOrder) override;
  //@}

  //! @name Accessors
  //@{
  UniquePtr clone() const {
    return UniquePtr(clone_());
  }
  /*!
    @brief Write value to a character data buffer.

    The user must ensure that the buffer has enough memory. Otherwise
    the call results in undefined behaviour.

    @note The byte order is required by the interface but not used by this
          method, so just use the default.

    @param buf Data buffer to write to.
    @param byteOrder Byte order. Not used.
    @return Number of characters written.
  */
  size_t copy(byte* buf, ByteOrder byteOrder = invalidByteOrder) const override;
  size_t count() const override;
  size_t size() const override;
  int64_t toInt64(size_t n = 0) const override;
  uint32_t toUint32(size_t n = 0) const override;
  float toFloat(size_t n = 0) const override;
  Rational toRational(size_t n = 0) const override;
  std::ostream& write(std::ostream& os) const override;
  //@}

 protected:
  //! Internal virtual copy constructor.
  StringValueBase* clone_() const override = 0;

 public:
  // DATA
  std::string value_;  //!< Stores the string value.

};  // class StringValueBase

/*!
  @brief %Value for string type.

  This can be a plain Ascii string or a multiple byte encoded string. It is
  left to caller to decode and encode the string to and from readable
  text if that is required.
*/
class EXIV2API StringValue : public StringValueBase {
 public:
  //! Shortcut for a %StringValue auto pointer.
  using UniquePtr = std::unique_ptr<StringValue>;

  //! @name Creators
  //@{
  //! Default constructor.
  StringValue();
  //! Constructor
  explicit StringValue(const std::string& buf);
  //@}

  //! @name Accessors
  //@{
  UniquePtr clone() const {
    return UniquePtr(clone_());
  }
  //@}

 private:
  //! Internal virtual copy constructor.
  StringValue* clone_() const override;

};  // class StringValue

/*!
  @brief %Value for an Ascii string type.

  This class is for null terminated single byte Ascii strings.
  This class also ensures that the string is null terminated.
 */
class EXIV2API AsciiValue : public StringValueBase {
 public:
  //! Shortcut for a %AsciiValue auto pointer.
  using UniquePtr = std::unique_ptr<AsciiValue>;

  //! @name Creators
  //@{
  //! Default constructor.
  AsciiValue();
  //! Constructor
  explicit AsciiValue(const std::string& buf);
  //@}

  //! @name Manipulators
  //@{
  using StringValueBase::read;
  /*!
    @brief Set the value to that of the string buf. Overrides base class
           to append a terminating '\\0' character if buf doesn't end
           with '\\0'.
   */
  int read(const std::string& buf) override;
  //@}

  //! @name Accessors
  //@{
  UniquePtr clone() const {
    return UniquePtr(clone_());
  }
  /*!
    @brief Write the ASCII value up to the first '\\0' character to an
           output stream.  Any further characters are ignored and not
           written to the output stream.
  */
  std::ostream& write(std::ostream& os) const override;
  //@}

 private:
  //! Internal virtual copy constructor.
  AsciiValue* clone_() const override;

};  // class AsciiValue

/*!
  @brief %Value for an Exif comment.

  This can be a plain Ascii string or a multiple byte encoded string. The
  comment is expected to be encoded in the character set indicated (default
  undefined), but this is not checked. It is left to caller to decode and
  encode the string to and from readable text if that is required.
*/
class EXIV2API CommentValue : public StringValueBase {
 public:
  //! Character set identifiers for the character sets defined by %Exif
  enum CharsetId { ascii, jis, unicode, undefined, invalidCharsetId, lastCharsetId };
  //! Information pertaining to the defined character sets
  struct CharsetTable {
    CharsetId charsetId_;  //!< Charset id
    const char* name_;     //!< Name of the charset
    const char* code_;     //!< Code of the charset
  };

  //! Charset information lookup functions. Implemented as a static class.
  class EXIV2API CharsetInfo {
   public:
    //! Return the name for a charset id
    static const char* name(CharsetId charsetId);
    //! Return the code for a charset id
    static const char* code(CharsetId charsetId);
    //! Return the charset id for a name
    static CharsetId charsetIdByName(const std::string& name);
    //! Return the charset id for a code
    static CharsetId charsetIdByCode(const std::string& code);

   private:
    static const CharsetTable charsetTable_[];
  };  // class CharsetInfo

  //! Shortcut for a %CommentValue auto pointer.
  using UniquePtr = std::unique_ptr<CommentValue>;

  //! @name Creators
  //@{
  //! Default constructor.
  CommentValue();
  //! Constructor, uses read(const std::string& comment)
  explicit CommentValue(const std::string& comment);
  //@}

  //! @name Manipulators
  //@{
  /*!
    @brief Read the value from a comment

    The format of \em comment is:
    <BR>
    <CODE>[charset=["]Ascii|Jis|Unicode|Undefined["] ]comment</CODE>
    <BR>
    The default charset is Undefined.

    @return 0 if successful<BR>
            1 if an invalid character set is encountered
  */
  int read(const std::string& comment) override;
  int read(const byte* buf, size_t len, ByteOrder byteOrder) override;
  //@}

  //! @name Accessors
  //@{
  UniquePtr clone() const {
    return UniquePtr(clone_());
  }
  size_t copy(byte* buf, ByteOrder byteOrder) const override;
  /*!
    @brief Write the comment in a format which can be read by
    read(const std::string& comment).
   */
  std::ostream& write(std::ostream& os) const override;
  /*!
    @brief Return the comment (without a charset="..." prefix)

    The comment is decoded to UTF-8. For Exif UNICODE comments, the
    function makes an attempt to correctly determine the character
    encoding of the value. Alternatively, the optional \em encoding
    parameter can be used to specify it.

    @param encoding Optional argument to specify the character encoding
        that the comment is encoded in, as an iconv(3) name. Only used
        for Exif UNICODE comments.

    @return A string containing the comment converted to UTF-8.
   */
  std::string comment(const char* encoding = nullptr) const;
  /*!
    @brief Determine the character encoding that was used to encode the
        UNICODE comment value as an iconv(3) name.

    If the comment \em c starts with a BOM, the BOM is interpreted and
    removed from the string.

    Todo: Implement rules to guess if the comment is UTF-8 encoded.
   */
  const char* detectCharset(std::string& c) const;
  //! Return the Exif charset id of the comment
  CharsetId charsetId() const;
  //@}

 private:
  //! Internal virtual copy constructor.
  CommentValue* clone_() const override;

 public:
  // DATA
  ByteOrder byteOrder_{littleEndian};  //!< Byte order of the comment string that was read

};  // class CommentValue

/*!
  @brief Base class for all Exiv2 values used to store XMP property values.
 */
class EXIV2API XmpValue : public Value {
  using Value::Value;

 public:
  //! Shortcut for a %XmpValue auto pointer.
  using UniquePtr = std::unique_ptr<XmpValue>;

  //! XMP array types.
  enum XmpArrayType { xaNone, xaAlt, xaBag, xaSeq };
  //! XMP structure indicator.
  enum XmpStruct { xsNone, xsStruct };

  //! @name Accessors
  //@{
  //! Return XMP array type, indicates if an XMP value is an array.
  XmpArrayType xmpArrayType() const;
  //! Return XMP struct, indicates if an XMP value is a structure.
  XmpStruct xmpStruct() const;
  size_t size() const override;
  /*!
    @brief Write value to a character data buffer.

    The user must ensure that the buffer has enough memory. Otherwise
    the call results in undefined behaviour.

    @note The byte order is required by the interface but not used by this
          method, so just use the default.

    @param buf Data buffer to write to.
    @param byteOrder Byte order. Not used.
    @return Number of characters written.
  */
  size_t copy(byte* buf, ByteOrder byteOrder = invalidByteOrder) const override;
  //@}

  //! @name Manipulators
  //@{
  //! Set the XMP array type to indicate that an XMP value is an array.
  void setXmpArrayType(XmpArrayType xmpArrayType);
  //! Set the XMP struct type to indicate that an XMP value is a structure.
  void setXmpStruct(XmpStruct xmpStruct = xsStruct);

  /// @note Uses read(const std::string& buf)
  int read(const byte* buf, size_t len, ByteOrder byteOrder = invalidByteOrder) override;
  int read(const std::string& buf) override = 0;
  //@}

  /*!
    @brief Return XMP array type for an array Value TypeId, xaNone if
           \em typeId is not an XMP array value type.
   */
  static XmpArrayType xmpArrayType(TypeId typeId);

 private:
  // DATA
  XmpArrayType xmpArrayType_{xaNone};  //!< Type of XMP array
  XmpStruct xmpStruct_{xsNone};        //!< XMP structure indicator

};  // class XmpValue

/*!
  @brief %Value type suitable for simple XMP properties and
         XMP nodes of complex types which are not parsed into
         specific values.

  Uses a std::string to store the value.
 */
class EXIV2API XmpTextValue : public XmpValue {
 public:
  //! Shortcut for a %XmpTextValue auto pointer.
  using UniquePtr = std::unique_ptr<XmpTextValue>;

  //! @name Creators
  //@{
  //! Constructor.
  XmpTextValue();
  //! Constructor, reads the value from a string.
  explicit XmpTextValue(const std::string& buf);
  //@}

  //! @name Manipulators
  //@{
  using XmpValue::read;
  /*!
    @brief Read a simple property value from \em buf to set the value.

    Sets the value to the contents of \em buf. A optional keyword,
    \em type is supported to set the XMP value type. This is useful for
    complex value types for which Exiv2 does not have direct support.

    The format of \em buf is:
    <BR>
    <CODE>[type=["]Alt|Bag|Seq|Struct["] ]text</CODE>
    <BR>

    @return 0 if successful.
   */

  int read(const std::string& buf) override;
  //@}

  //! @name Accessors
  //@{
  UniquePtr clone() const;
  size_t size() const override;
  size_t count() const override;
  /*!
    @brief Convert the value to an int64_t.
           The optional parameter \em n is not used and is ignored.

    @return The converted value.
   */
  int64_t toInt64(size_t n = 0) const override;
  /*!
    @brief Convert the value to an uint32_t.
           The optional parameter \em n is not used and is ignored.

    @return The converted value.
   */
  uint32_t toUint32(size_t n = 0) const override;
  /*!
    @brief Convert the value to a float.
           The optional parameter \em n is not used and is ignored.

    @return The converted value.
   */
  float toFloat(size_t n = 0) const override;
  /*!
    @brief Convert the value to a Rational.
           The optional parameter \em n is not used and is ignored.

    @return The converted value.
   */
  Rational toRational(size_t n = 0) const override;
  std::ostream& write(std::ostream& os) const override;
  //@}

 private:
  //! Internal virtual copy constructor.
  XmpTextValue* clone_() const override;

 public:
  // DATA
  std::string value_;  //!< Stores the string values.

};  // class XmpTextValue

/*!
  @brief %Value type for simple arrays. Each item in the array is a simple
         value, without qualifiers. The array may be an ordered (\em seq),
         unordered (\em bag) or alternative array (\em alt). The array
         items must not contain qualifiers. For language alternatives use
         LangAltValue.

  Uses a vector of std::string to store the value(s).
 */
class EXIV2API XmpArrayValue : public XmpValue {
 public:
  //! Shortcut for a %XmpArrayValue auto pointer.
  using UniquePtr = std::unique_ptr<XmpArrayValue>;

  //! @name Creators
  //@{
  //! Constructor. \em typeId can be one of xmpBag, xmpSeq or xmpAlt.
  explicit XmpArrayValue(TypeId typeId = xmpBag);
  //@}

  //! @name Manipulators
  //@{
  using XmpValue::read;
  /*!
    @brief Read a simple property value from \em buf and append it
           to the value.

    Appends \em buf to the value after the last existing array element.
    Subsequent calls will therefore populate multiple array elements in
    the order they are read.

    @return 0 if successful.
   */
  int read(const std::string& buf) override;
  //@}

  //! @name Accessors
  //@{
  UniquePtr clone() const;
  size_t count() const override;
  /*!
    @brief Return the <EM>n</EM>-th component of the value as a string.
           The behaviour of this method may be undefined if there is no
           <EM>n</EM>-th component.
   */
  std::string toString(size_t n) const override;
  int64_t toInt64(size_t n = 0) const override;
  uint32_t toUint32(size_t n = 0) const override;
  float toFloat(size_t n = 0) const override;
  Rational toRational(size_t n = 0) const override;
  /*!
    @brief Write all elements of the value to \em os, separated by commas.

    @note The output of this method cannot directly be used as the parameter
          for read().
   */
  std::ostream& write(std::ostream& os) const override;
  //@}

 private:
  //! Internal virtual copy constructor.
  XmpArrayValue* clone_() const override;

  std::vector<std::string> value_;  //!< Stores the string values.

};  // class XmpArrayValue

/*!
  @brief %LangAltValueComparator

  #1058
  https://www.adobe.com/content/dam/Adobe/en/devnet/xmp/pdfs/XMPSpecificationPart1.pdf
  XMP spec chapter B.4 (page 42) the xml:lang qualifier is to be compared case insensitive.
  */
struct LangAltValueComparator {
  //! LangAltValueComparator comparison case insensitive function
  bool operator()(const std::string& str1, const std::string& str2) const {
    int result = str1.size() < str2.size() ? 1 : str1.size() > str2.size() ? -1 : 0;
    if (result == 0) {
      for (auto c1 = str1.begin(), c2 = str2.begin(); result == 0 && c1 != str1.end(); ++c1, ++c2) {
        result = tolower(*c1) < tolower(*c2) ? 1 : tolower(*c1) > tolower(*c2) ? -1 : 0;
      }
    }
    return result < 0;
  }
};

/*!
  @brief %Value type for XMP language alternative properties.

  A language alternative is an array consisting of simple text values,
  each of which has a language qualifier.
 */
class EXIV2API LangAltValue : public XmpValue {
 public:
  //! Shortcut for a %LangAltValue auto pointer.
  using UniquePtr = std::unique_ptr<LangAltValue>;

  //! @name Creators
  //@{
  //! Constructor.
  LangAltValue();
  //! Constructor, reads the value from a string.
  explicit LangAltValue(const std::string& buf);
  //@}

  //! @name Manipulators
  //@{
  using XmpValue::read;
  /*!
    @brief Read a simple property value from \em buf and append it
           to the value.

    Appends \em buf to the value after the last existing array element.
    Subsequent calls will therefore populate multiple array elements in
    the order they are read.

    The format of \em buf is:
    <BR>
    <CODE>[lang=["]language code["] ]text</CODE>
    <BR>
    The XMP default language code <CODE>x-default</CODE> is used if
    \em buf doesn't start with the keyword <CODE>lang</CODE>.

    @return 0 if successful.
   */
  int read(const std::string& buf) override;
  //@}

  //! @name Accessors
  //@{
  UniquePtr clone() const;
  size_t count() const override;
  /*!
    @brief Return the text value associated with the default language
           qualifier \c x-default. The parameter \em n is not used, but
           it is suggested that only 0 is passed in. Returns an empty
           string and sets the ok-flag to \c false if there is no
           default value.
   */
  std::string toString(size_t n) const override;
  /*!
    @brief Return the text value associated with the language qualifier
           \em qualifier. Returns an empty string and sets the ok-flag
           to \c false if there is no entry for the language qualifier.
   */
  std::string toString(const std::string& qualifier) const;
  int64_t toInt64(size_t n = 0) const override;
  uint32_t toUint32(size_t n = 0) const override;
  float toFloat(size_t n = 0) const override;
  Rational toRational(size_t n = 0) const override;
  /*!
    @brief Write all elements of the value to \em os, separated by commas.

    @note The output of this method cannot directly be used as the parameter
          for read().
   */
  std::ostream& write(std::ostream& os) const override;
  //@}

 private:
  //! Internal virtual copy constructor.
  LangAltValue* clone_()