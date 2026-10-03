// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef XMP_HPP_
#define XMP_HPP_

// *****************************************************************************
#include "exiv2lib_export.h"

// included header files
#include "metadatum.hpp"
#include "properties.hpp"

// *****************************************************************************
// namespace extensions
namespace Exiv2 {
// *****************************************************************************
// class declarations
class ExifData;

// *****************************************************************************
// class definitions

/*!
  @brief Information related to an XMP property. An XMP metadatum consists
         of an XmpKey and a Value and provides methods to manipulate these.
 */
class EXIV2API Xmpdatum : public Metadatum {
 public:
  //! @name Creators
  //@{
  /*!
    @brief Constructor for new tags created by an application. The
           %Xmpdatum is created from a key / value pair. %Xmpdatum
           copies (clones) the value if one is provided. Alternatively, a
           program can create an 'empty' %Xmpdatum with only a key and
           set the value using setValue().

    @param key The key of the %Xmpdatum.
    @param pValue Pointer to a %Xmpdatum value.
    @throw Error if the key cannot be parsed and converted
           to a known schema namespace prefix and property name.
   */
  explicit Xmpdatum(const XmpKey& key, const Value* pValue = nullptr);
  //! Copy constructor
  Xmpdatum(const Xmpdatum& rhs);
  //! Destructor
  ~Xmpdatum() override;
  //@}

  //! @name Manipulators
  //@{
  //! Assignment operator
  Xmpdatum& operator=(const Xmpdatum& rhs);
  /*!
    @brief Assign std::string \em value to the %Xmpdatum.
           Calls setValue(const std::string&).
   */
  Xmpdatum& operator=(const std::string& value);
  /*!
    @brief Assign a \em value of any type with an output operator
           to the %Xmpdatum. Calls operator=(const std::string&).
   */
  template <typename T>
  Xmpdatum& operator=(const T& value);
  /*!
    @brief Assign Value \em value to the %Xmpdatum.
           Calls setValue(const Value*).
   */
  Xmpdatum& operator=(const Value& value);
  void setValue(const Value* pValue) override;
  /*!
    @brief Set the value to the string \em value. Uses Value::read(const
           std::string&).  If the %Xmpdatum does not have a Value yet,
           then a %Value of the correct type for this %Xmpdatum is
           created. If the key is unknown, a XmpTextValue is used as
           default. Return 0 if the value was read successfully.
   */
  int setValue(const std::string& value) override;
 