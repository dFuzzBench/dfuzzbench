from __future__ import absolute_import, division, unicode_literals

import string

EOF = None

E = {
    "null-character":
        "Null character in input stream, replaced with U+FFFD.",
    "invalid-codepoint":
        "Invalid codepoint in stream.",
    "incorrectly-placed-solidus":
        "Solidus (/) incorrectly placed in tag.",
    "incorrect-cr-newline-entity":
        "Incorrect CR newline entity, replaced with LF.",
    "illegal-windows-1252-entity":
        "Entity used with illegal number (windows-1252 reference).",
    "cant-convert-numeric-entity":
        "Numeric entity couldn't be converted to character "
        "(codepoint U+%(charAsInt)08x).",
    "illegal-codepoint-for-numeric-entity":
        "Numeric entity represents an illegal codepoint: "
        "U+%(charAsInt)08x.",
    "numeric-entity-without-semicolon":
        "Numeric entity didn't end with ';'.",
    "expected-numeric-entity-but-got-eof":
        "Numeric entity expected. Got end of file instead.",
    "expected-numeric-entity":
        "Numeric entity expected but none found.",
    "named-entity-without-semicolon":
        "Named entity didn't end with ';'.",
    "expected-named-entity":
        "Named entity expected. Got none.",
    "attributes-in-end-tag":
        "End tag contains unexpected attributes.",
    'self-closing-flag-on-end-tag':
        "End tag contains unexpected self-closing flag.",
    "expected-tag-name-but-got-right-bracket":
        "Expected tag name. Got '>' instead.",
    "expected-tag-name-but-got-question-mark":
        "Expected tag name. Got '?' instead. (HTML doesn't "
        "support processing instructions.)",
    "expected-tag-name":
        "Expected tag name. Got something else instead",
    "expected-closing-tag-but-got-right-bracket":
        "Expected closing tag. Got '>' instead. Ignoring '</>'.",
    "expected-closing-tag-but-got-eof":
        "Expected closing tag. Unexpected end of file.",
    "expected-closing-tag-but-got-char":
        "Expected closing tag. Unexpected character '%(data)s' found.",
    "eof-in-tag-name":
        "Unexpected end of file in the tag name.",
    "expected-attribute-name-but-got-eof":
        "Unexpected end of file. Expected attribute name instead.",
    "eof-in-attribute-name":
        "Unexpected end of file in attribute name.",
    "invalid-character-in-attribute-name":
        "Invalid character in attribute name",
    "duplicate-attribute":
        "Dropped duplicate attribute on tag.",
    "expected-end-of-tag-name-but-got-eof":
        "Unexpected end of file. Expected = or end of tag.",
    "expected-attribute-value-but-got-eof":
        "Unexpected end of file. Expected attribute value.",
    "expected-attribute-value-but-got-right-bracket":
        "Expected attribute value. Got '>' instead.",
    'equals-in-unquoted-attribute-value':
        "Unexpected = in unquoted attribute",
    'unexpected-character-in-unquoted-attribute-value':
        "Unexpected character in unquoted attribute",
    "invalid-character-after-attribute-name":
        "Unexpected character after attribute name.",
    "unexpected-character-after-attribute-value":
        "Unexpected character after attribute value.",
    "eof-in-attribute-value-double-quote":
        "Unexpected end of file in attribute value (\").",
    "eof-in-attribute-value-single-quote":
        "Unexpected end of file in attribute value (').",
    "eof-in-attribute-value-no-quotes":
        "Unexpected end of file in attribute value.",
    "unexpected-EOF-after-solidus-in-tag":
        "Unexpected end of file in tag. Expected >",
    "unexpected-character-after-solidus-in-tag":
        "Unexpected character after / in tag. Expected >",
    "expected-dashes-or-doctype":
        "Expected '--' or 'DOCTYPE'. Not found.",
    "unexpected-bang-after-double-dash-in-comment":
        "Unexpected ! after -- in comment",
    "unexpected-space-after-double-dash-in-comment":
        "Unexpected space after -- in comment",
    "incorrect-comment":
        "Incorrect comment.",
    "eof-in-comment":
        "Unexpected end of file in comment.",
    "eof-in-comment-end-dash":
        "Unexpected end of file in comment (-)",
    "unexpected-dash-after-double-dash-in-comment":
        "Unexpected '-' after '--' found in comment.",
    "eof-in-comment-double-dash":
        "Unexpected end of file in comment (--).",
    "eof-in-comment-end-space-state":
        "Unexpected end of file in comment.",
    "eof-in-comment-end-bang-state":
        "Unexpected end of file in comment.",
    "unexpected-char-in-comment":
        "Unexpected character in comment found.",
    "need-space-after-doctype":
        "No space after literal string 'DOCTYPE'.",
    "expected-doctype-name-but-got-right-bracket":
        "Unexpected > character. Expected DOCTYPE name.",
    "expected-doctype-name-but-got-eof":
        "Unexpected end of file. Expected DOCTYPE name.",
    "eof-in-doctype-name":
        "Unexpected end of file in DOCTYPE name.",
    "eof-in-doctype":
        "Unexpected end of file in DOCTYPE.",
    "expected-space-or-right-bracket-in-doctype":
        "Expected space or '>'. Got '%(data)s'",
    "unexpected-end-of-doctype":
        "Unexpected end of DOCTYPE.",
    "unexpected-char-in-doctype":
        "Unexpected character in DOCTYPE.",
    "eof-in-innerhtml":
        "XXX innerHTML EOF",
    "unexpected-doctype":
        "Unexpected DOCTYPE. Ignored.",
    "non-html-root":
        "html needs to be the first start tag.",
    "expected-doctype-but-got-eof":
        "Unexpected End of file. Expected DOCTYPE.",
    "unknown-doctype":
        "Erroneous DOCTYPE.",
    "expected-doctype-but-got-chars":
        "Unexpected non-space characters. Expected DOCTYPE.",
    "expected-doctype-but-got-start-tag":
        "Unexpected start tag (%(name)s). Expected DOCTYPE.",
    "expected-doctype-but-got-end-tag":
        "Unexpected end tag (%(name)s). Expected DOCTYPE.",
    "end-tag-after-implied-root":
        "Unexpected end tag (%(name)s) after the (implied) root element.",
    "expected-named-closing-tag-but-got-eof":
        "Unexpected end of file. Expected end tag (%(name)s).",
    "two-heads-are-not-better-than-one":
        "Unexpected start tag head in existing head. Ignored.",
    "unexpected-end-tag":
        "Unexpected end tag (%(name)s). Ignored.",
    "unexpected-start-tag-out-of-my-head":
        "Unexpected start tag (%(name)s) that can be in head. Moved.",
    "unexpected-start-tag":
        "Unexpected start tag (%(name)s).",
    "missing-end-tag":
        "Missing end tag (%(name)s).",
    "missing-end-tags":
        "Missing end tags (%(name)s).",
    "unexpected-start-tag-implies-end-tag":
        "Unexpected start tag (%(startName)s) "
        "implies end tag (%(endName)s).",
    "unexpected-start-tag-treated-as":
        "Unexpected start tag (%(originalName)s). Treated as %(newName)s.",
    "deprecated-tag":
        "Unexpected start tag %(name)s. Don't use it!",
    "unexpected-start-tag-ignored":
        "Unexpected start tag %(name)s. Ignored.",
    "expected-one-end-tag-but-got-another":
        "Unexpected end tag (%(gotName)s). "
        "Missing end tag (%(expectedName)s).",
    "end-tag-too-early":
        "End tag (%(name)s) seen too early. Expected other end tag.",
    "end-tag-too-early-named":
        "Unexpected end tag (%(gotName)s). Expected end tag (%(expectedName)s).",
    "end-tag-too-early-ignored":
        "End tag (%(name)s) seen too early. Ignored.",
    "adoption-agency-1.1":
        "End tag (%(name)s) violates step 1, "
        "paragraph 1 of the adoption agency algorithm.",
    "adoption-agency-1.2":
        "End tag (%(name)s) violates step 1, "
        "paragraph 2 of the adoption agency algorithm.",
    "adoption-agency-1.3":
        "End tag (%(name)s) violates step 1, "
        "paragraph 3 of the adoption agency algorithm.",
    "adoption-agency-4.4":
        "End tag (%(name)s) violates step 4, "
        "paragraph 4 of the adoption agency algorithm.",
    "unexpected-end-tag-treated-as":
        "Unexpected end tag (%(originalName)s). Treated as %(newName)s.",
    "no-end-tag":
        "This element (%(name)s) has no end tag.",
    "unexpected-implied-end-tag-in-table":
        "Unexpected implied end tag (%(name)s) in the table phase.",
    "unexpected-implied-end-tag-in-table-body":
        "Unexpected implied end tag (%(name)s) in the table body phase.",
    "unexpected-char-implies-table-voodoo":
        "Unexpected non-space characters in "
        "table context caused voodoo mode.",
    "unexpected-hidden-input-in-table":
        "Unexpected input with type hidden in table context.",
    "unexpected-form-in-table":
        "Unexpected form in table context.",
    "unexpected-start-tag-implies-table-voodoo":
        "Unexpected start tag (%(name)s) in "
        "table context caused voodoo mode.",
    "unexpected-end-tag-implies-table-voodoo":
        "Unexpected end tag (%(name)s) in "
        "table context caused voodoo mode.",
    "unexpected-cell-in-table-body":
        "Unexpected table cell start tag (%(name)s) "
        "in the table body phase.",
    "unexpected-cell-end-tag":
        "Got table cell end tag (%(name)s) "
        "while required end tags are missing.",
    "unexpected-end-tag-in-table-body":
        "Unexpected end tag (%(name)s) in the table body phase. Ignored.",
    "unexpected-implied-end-tag-in-table-row":
        "Unexpected implied end tag (%(name)s) in the table row phase.",
    "unexpected-end-tag-in-table-row":
        "Unexpected end tag (%(name)s) in the table row phase. Ignored.",
    "unexpected-select-in-select":
        "Unexpected select start tag in the select phase "
        "treated as select end tag.",
    "unexpected-input-in-select":
        "Unexpected input start tag in the select phase.",
    "unexpected-start-tag-in-select":
        "Unexpected start tag token (%(name)s in the select phase. "
        "Ignored.",
    "unexpected-end-tag-in-select":
        "Unexpected end tag (%(name)s) in the select phase. Ignored.",
    "unexpected-table-element-start-tag-in-select-in-table":
        "Unexpected table element start tag (%(name)s) in the select in table phase.",
    "unexpected-table-element-end-tag-in-select-in-table":
        "Unexpected table element end tag (%(name)s) in the select in table phase.",
    "unexpected-char-after-body":
        "Unexpected non-space characters in the after body phase.",
    "unexpected-start-tag-after-body":
        "Unexpected start tag token (%(name)s)"
        " in the after body phase.",
    "unexpected-end-tag-after-body":
        "Unexpected end tag token (%(name)s)"
        " in the after body phase.",
    "unexpected-char-in-frameset":
        "Unexpected characters in the frameset phase. Characters ignored.",
    "unexpected-start-tag-in-frameset":
        "Unexpected start tag token (%(name)s)"
        " in the frameset phase. Ignored.",
    "unexpected-frameset-in-frameset-innerhtml":
        "Unexpected end tag token (frameset) "
        "in the frameset phase (innerHTML).",
    "unexpected-end-tag-in-frameset":
        "Unexpected end tag token (%(name)s)"
        " in the frameset phase. Ignored.",
    "unexpected-char-after-frameset":
        "Unexpected non-space characters in the "
        "after frameset phase. Ignored.",
    "unexpected-start-tag-after-frameset":
        "Unexpected start tag (%(name)s)"
        " in the after frameset phase. Ignored.",
    "unexpected-end-tag-after-frameset":
        "Unexpected end tag (%(name)s)"
        " in the after frameset phase. Ignored.",
    "unexpected-end-tag-after-body-innerhtml":
        "Unexpected end tag after body(innerHtml)",
    "expected-eof-but-got-char":
        "Unexpected non-space characters. Expected end of file.",
    "expected-eof-but-got-start-tag":
        "Unexpected start tag (%(name)s)"
        ". Expected end of file.",
    "expected-eof-but-got-end-tag":
        "Unexpected end tag (%(name)s)"
        ". Expected end of file.",
    "eof-in-table":
        "Unexpected end of file. Expected table content.",
    "eof-in-select":
        "Unexpected end of file. Expected select content.",
    "eof-in-frameset":
        "Unexpected end of file. Expected frameset content.",
    "eof-in-script-in-script":
        "Unexpected end of file. Expected script content.",
    "eof-in-foreign-lands":
        "Unexpected end of file. Expected foreign content",
    "non-void-element-with-trailing-solidus":
        "Trailing solidus not allowed on element %(name)s",
    "unexpected-html-element-in-foreign-content":
        "Element %(name)s not allowed in a non-html context",
    "unexpected-end-tag-before-html":
        "Unexpected end tag (%(name)s) before html.",
    "unexpected-inhead-noscript-tag":
        "Element %(name)s not allowed in a inhead-noscript context",
    "eof-in-head-noscript":
        "Unexpected end of file. Expected inhead-noscript content",
    "char-in-head-noscript":
        "Unexpected non-space character. Expected inhead-noscript content",
    "XXX-undefined-error":
        "Undefined error (this sucks and should be fixed)",
}

namespaces = {
    "html": "http://www.w3.org/1999/xhtml",
    "mathml": "http://www.w3.org/1998/Math/MathML",
    "svg": "http://www.w3.org/2000/svg",
    "xlink": "http://www.w3.org/1999/xlink",
    "xml": "http://www.w3.org/XML/1998/namespace",
    "xmlns": "http://www.w3.org/2000/xmlns/"
}

scopingElements = frozenset([
    (namespaces["html"], "applet"),
    (namespaces["html"], "caption"),
    (namespaces["html"], "html"),
    (namespaces["html"], "marquee"),
    (namespaces["html"], "object"),
    (namespaces["html"], "table"),
    (namespaces["html"], "td"),
    (namespaces["html"], "th"),
    (namespaces["mathml"], "mi"),
    (namespaces["mathml"], "mo"),
    (namespaces["mathml"], "mn"),
    (namespaces["mathml"], "ms"),
    (namespaces["mathml"], "mtext"),
    (namespaces["mathml"], "annotation-xml"),
    (namespaces["svg"], "foreignObject"),
    (namespaces["svg"], "desc"),
    (namespaces["svg"], "title"),
])

formattingElements = frozenset([
    (namespaces["html"], "a"),
    (namespaces["html"], "b"),
    (namespaces["html"], "big"),
    (namespaces["html"], "code"),
    (namespaces["html"], "em"),
    (namespaces["html"], "font"),
    (namespaces["html"], "i"),
    (namespaces["html"], "nobr"),
    (namespaces["html"], "s"),
    (namespaces["html"], "small"),
    (namespaces["html"], "strike"),
    (namespaces["html"], "strong"),
    (namespaces["html"], "tt"),
    (namespaces["html"], "u")
])

specialElements = frozenset([
    (namespaces["html"], "address"),
    (namespaces["html"], "applet"),
    (namespaces["html"], "area"),
    (namespaces["html"], "article"),
    (namespaces["html"], "aside"),
    (namespaces["html"], "base"),
    (namespaces["html"], "basefont"),
    (namespaces["html"], "bgsound"),
    (namespaces["html"], "blockquote"),
    (namespaces["html"], "body"),
    (namespaces["html"], "br"),
    (namespaces["html"], "button"),
    (namespaces["html"], "caption"),
    (namespaces["html"], "center"),
    (namespaces["html"], "col"),
    (namespaces["html"], "colgroup"),
    (namespaces["html"], "command"),
    (namespaces["html"], "dd"),
    (namespaces["html"], "details"),
    (namespaces["html"], "dir"),
    (namespaces["html"], "div"),
    (namespaces["html"], "dl"),
    (namespaces["html"], "dt"),
    (namespaces["html"], "embed"),
    (namespaces["html"], "fieldset"),
    (namespaces["html"], "figure"),
    (namespaces["html"], "footer"),
    (namespaces["html"], "form"),
    (namespaces["html"], "frame"),
    (namespaces["html"], "frameset"),
    (namespaces["html"], "h1"),
    (namespaces["html"], "h2"),
    (namespaces["html"], "h3"),
    (namespaces["html"], "h4"),
    (namespaces["html"], "h5"),
    (namespaces["html"], "h6"),
    (namespaces["html"], "head"),
    (namespaces["html"], "header"),
    (namespaces["html"], "hr"),
    (namespaces["html"], "html"),
    (namespaces["html"], "iframe"),
    # Note that image is commented out in the spec as "this isn't an
    # element that can end up on the stack, so it doesn't matter,"
    (namespaces["html"], "image"),
    (namespaces["html"], "img"),
    (namespaces["html"], "input"),
    (namespaces["html"], "isindex"),
    (namespaces["html"], "li"),
    (namespaces["html"], "link"),
    (namespaces["html"], "listing"),
    (namespaces["html"], "marquee"),
    (namespaces["html"], "menu"),
    (namespaces["html"], "meta"),
    (namespaces["html"], "nav"),
    (namespaces["html"], "noembed"),
    (namespaces["html"], "noframes"),
    (namespaces["html"], "noscript"),
    (namespaces["html"], "object"),
    (namespaces["html"], "ol"),
    (namespaces["html"], "p"),
    (namespaces["html"], "param"),
    (namespaces["html"], "plaintext"),
    (namespaces["html"], "pre"),
    (namespaces["html"], "script"),
    (namespaces["html"], "section"),
    (namespaces["html"], "select"),
    (namespaces["html"], "style"),
    (namespaces["html"], "table"),
    (namespaces["html"], "tbody"),
    (namespaces["html"], "td"),
    (namespaces["html"], "textarea"),
    (namespaces["html"], "tfoot"),
    (namespaces["html"], "th"),
    (namespaces["html"], "thead"),
    (namespaces["html"], "title"),
    (namespaces["html"], "tr"),
    (namespaces["html"], "ul"),
    (namespaces["html"], "wbr"),
    (namespaces["html"], "xmp"),
    (namespaces["svg"], "foreignObject")
])

htmlIntegrationPointElements = frozenset([
    (namespaces["mathml"], "annotation-xml"),
    (namespaces["svg"], "foreignObject"),
    (namespaces["svg"], "desc"),
    (namespaces["svg"], "title")
])

mathmlTextIntegrationPointElements = frozenset([
    (namespaces["mathml"], "mi"),
    (namespaces["mathml"], "mo"),
    (namespaces["mathml"], "mn"),
    (namespaces["mathml"], "ms"),
    (namespaces["mathml"], "mtext")
])

adjustSVGAttributes = {
    "attributename": "attributeName",
    "attributetype": "attributeType",
    "basefrequency": "baseFrequency",
    "baseprofile": "baseProfile",
    "calcmode": "calcMode",
    "clippathunits": "clipPathUnits",
    "contentscripttype": "contentScriptType",
    "contentstyletype": "contentStyleType",
    "diffuseconstant": "diffuseConstant",
    "edgemode": "edgeMode",
    "externalresourcesrequired": "externalResourcesRequired",
    "filterres": "filterRes",
    "filterunits": "filterUnits",
    "glyphref": "glyphRef",
    "gradienttransform": "gradientTransform",
    "gradientunits": "gradientUnits",
    "kernelmatrix": "kernelMatrix",
    "kernelunitlength": "kernelUnitLength",
    "keypoints": "keyPoints",
    "keysplines": "keySplines",
    "keytimes": "keyTimes",
    "lengthadjust": "lengthAdjust",
    "limitingconeangle": "limitingConeAngle",
    "markerheight": "markerHeight",
    "markerunits": "markerUnits",
    "markerwidth": "markerWidth",
    "maskcontentunits": "maskContentUnits",
    "maskunits": "maskUnits",
    "numoctaves": "numOctaves",
    "pathlength": "pathLength",
    "patterncontentunits": "patternContentUnits",
    "patterntransform": "patternTransform",
    "patternunits": "patternUnits",
    "pointsatx": "pointsAtX",
    "pointsaty": "pointsAtY",
    "pointsatz": "pointsAtZ",
    "preservealpha": "preserveAlpha",
    "preserveaspectratio": "preserveAspectRatio",
    "primitiveunits": "primitiveUnits",
    "refx": "refX",
    "refy": "refY",
    "repeatcount": "repeatCount",
    "repeatdur": "repeatDur",
    "requiredextensions": "requiredExtensions",
    "requiredfeatures": "requiredFeatures",
    "specularconstant": "specularConstant",
    "specularexponent": "specularExponent",
    "spreadmethod": "spreadMethod",
    "startoffset": "startOffset",
    "stddeviation": "stdDeviation",
    "stitchtiles": "stitchTiles",
    "surfacescale": "surfaceScale",
    "systemlanguage": "systemLanguage",
    "tablevalues": "tableValues",
    "targetx": "targetX",
    "targety": "targetY",
    "textlength": "textLength",
    "viewbox": "viewBox",
    "viewtarget": "viewTarget",
    "xchannelselector": "xChannelSelector",
    "ychannelselector": "yChannelSelector",
    "zoomandpan": "zoomAndPan"
}

adjustMathMLAttributes = {"definitionurl": "definitionURL"}

adjustForeignAttributes = {
    "xlink:actuate": ("xlink", "actuate", namespaces["xlink"]),
    "xlink:arcrole": ("xlink", "arcrole", namespaces["xlink"]),
    "xlink:href": ("xlink", "href", namespaces["xlink"]),
    "xlink:role": ("xlink", "role", namespaces["xlink"]),
    "xlink:show": ("xlink", "show", namespaces["xlink"]),
    "xlink:title": ("xlink", "title", namespaces["xlink"]),
    "xlink:type": ("xlink", "type", namespaces["xlink"]),
    "xml:base": ("xml", "base", namespaces["xml"]),
    "xml:lang": ("xml", "lang", namespaces["xml"]),
    "xml:space": ("xml", "space", namespaces["xml"]),
    "xmlns": (None, "xmlns", namespaces["xmlns"]),
    "xmlns:xlink": ("xmlns", "xlink", namespaces["xmlns"])
}

unadjustForeignAttributes = {(ns, local): qname for qname, (prefix, local, ns) in
                             adjustForeignAttributes.items()}

spaceCharacters = frozenset([
    "\t",
    "\n",
    "\u000C",
    " ",
    "\r"
])

tableInsertModeElements = frozenset([
    "table",
    "tbody",
    "tfoot",
    "thead",
    "tr"
])

asciiLowercase = frozenset(string.ascii_lowercase)
asciiUppercase = frozenset(string.ascii_uppercase)
asciiLetters = frozenset(string.ascii_letters)
digits = frozenset(string.digits)
hexDigits = frozenset(string.hexdigits)

asciiUpper2Lower = {ord(c): ord(c.lower()) for c in string.ascii_uppercase}

# Heading elements need to be ordered
headingElements = (
    "h1",
    "h2",
    "h3",
    "h4",
    "h5",
    "h6"
)

voidElements = frozenset([
    "base",
    "command",
    "event-source",
    "link",
    "meta",
    "hr",
    "br",
    "img",
    "embed",
    "param",
    "area",
    "col",
    "input",
    "source",
    "track"
])

cdataElements = frozenset(['title', 'textarea'])

rcdataElements = frozenset([
    'style',
    'script',
    'xmp',
    'iframe',
    'noembed',
    'noframes',
    'noscript'
])

booleanAttributes = {
    "": frozenset(["irrelevant", "itemscope"]),
    "style": frozenset(["scoped"]),
    "img": frozenset(["ismap"]),
    "audio": frozenset(["autoplay", "controls"]),
    "video": frozenset(["autoplay", "controls"]),
    "script": frozenset(["defer", "async"]),
    "details": frozenset(["open"]),
    "datagrid": frozenset(["multiple", "disabled"]),
    "command": frozenset(["hidden", "disabled", "checked", "default"]),
    "hr": frozenset(["noshade"]),
    "menu": frozenset(["autosubmit"]),
    "fieldset": frozenset(["disabled", "readonly"]),
    "option": frozenset(["disabled", "readonly", "selected"]),
    "optgroup": frozenset(["disabled", "readonly"]),
    "button": frozenset(["disabled", "autofocus"]),
    "input": frozenset(["disabled", "readonly", "required", "autofocus", "checked", "ismap"]),
    "select": frozenset(["disabled", "readonly", "autofocus", "multiple"]),
    "output": frozenset(["disabled", "readonly"]),
    "iframe": frozenset(["seamless"]),
}

# entitiesWindows1252 has to be _ordered_ and needs to have an index. It
# therefore can't be a frozenset.
entitiesWindows1252 = (
    8364,   # 0x80  0x20AC  EURO SIGN
    65533,  # 0x81          UNDEFINED
    8218,   # 0x82  0x201A  SINGLE LOW-9 QUOTATION MARK
    402,    # 0x83  0x0192  LATIN SMALL LETTER F WITH HOOK
    8222,   # 0x84  0x201E  DOUBLE LOW-9 QUOTATION MARK
    8230,   # 0x85  0x2026  HORIZONTAL ELLIPSIS
    8224,   # 0x86  0x2020  DAGGER
    8225,   # 0x87  0x2021  DOUBLE DAGGER
    710,    # 0x88  0x02C6  MODIFIER LETTER CIRCUMFLEX ACCENT
    8240,   # 0x89  0x2030  PER MILLE SIGN
    352,    # 0x8A  0x0160  LATIN CAPITAL LETTER S WITH CARON
    8249,   # 0x8B  0x2039  SINGLE LEFT-POINTING ANGLE QUOTATION MARK
    338,    # 0x8C  0x0152  LATIN CAPITAL LIGATURE OE
    65533,  # 0x8D          UNDEFINED
    381,    # 0x8E  0x017D  LATIN CAPITAL LETTER Z WITH CARON
    65533,  # 0x8F          UNDEFINED
    65533,  # 0x90          UNDEFINED
    8216,   # 0x91  0x2018  LEFT SINGLE QUOTATION MARK
    8217,   # 0x92  0x2019  RIGHT SINGLE QUOTATION MARK
    8220,   # 0x93  0x201C  LEFT DOUBLE QUOTATION MARK
    8221,   # 0x94  0x201D  RIGHT DOUBLE QUOTATION MARK
    8226,   # 0x95  0x2022  BULLET
    8211,   # 0x96  0x2013  EN DASH
    8212,   # 0x97  0x2014  EM DASH
    732,    # 0x98  0x02DC  SMALL TILDE
    8482,   # 0x99  0x2122  TRADE MARK SIGN
    353,    # 0x9A  0x0161  LATIN SMALL LETTER S WITH CARON
    8250,   # 0x9B  0x203A  SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
    339,    # 0x9C  0x0153  LATIN SMALL LIGATURE OE
    65533,  # 0x9D          UNDEFINED
    382,    # 0x9E  0x017E  LATIN SMALL LETTER Z WITH CARON
    376     # 0x9F  0x0178  LATIN CAPITAL LETTER Y WITH DIAERESIS
)

xmlEntities = frozenset(['lt;', 'gt;', 'amp;', 'apos;', 'quot;'])

entities = {
    "AElig": "\xc6",
    "AElig;": "\xc6",
    "AMP": "&",
    "AMP;": "&",
    "Aacute": "\xc1",
    "Aacute;": "\xc1",
    "Abreve;": "\u0102",
    "Acirc": "\xc2",
    "Acirc;": "\xc2",
    "Acy;": "\u0410",
    "Afr;": "\U0001d504",
    "Agrave": "\xc0",
    "Agrave;": "\xc0",
    "Alpha;": "\u0391",
    "Amacr;": "\u0100",
    "And;": "\u2a53",
    "Aogon;": "\u0104",
    "Aopf;": "\U0001d538",
    "ApplyFunction;": "\u2061",
    "Aring": "\xc5",
    "Aring;": "\xc5",
    "Ascr;": "\U0001d49c",
    "Assign;": "\u2254",
    "Atilde": "\xc3",
    "Atilde;": "\xc3",
    "Auml": "\xc4",
    "Auml;": "\xc4",
    "Backslash;": "\u2216",
    "Barv;": "\u2ae7",
    "Barwed;": "\u2306",
    "Bcy;": "\u0411",
    "Because;": "\u2235",
    "Bernoullis;": "\u212c",
    "Beta;": "\u0392",
    "Bfr;": "\U0001d505",
    "Bopf;": "\U0001d539",
    "Breve;": "\u02d8",
    "Bscr;": "\u212c",
    "Bumpeq;": "\u224e",
    "CHcy;": "\u0427",
    "COPY": "\xa9",
    "COPY;": "\xa9",
    "Cacute;": "\u0106",
    "Cap;": "\u22d2",
    "CapitalDifferentialD;": "\u2145",
    "Cayleys;": "\u212d",
    "Ccaron;": "\u010c",
    "Ccedil": "\xc7",
    "Ccedil;": "\xc7",
    "Ccirc;": "\u0108",
    "Cconint;": "\u2230",
    "Cdot;": "\u010a",
    "Cedilla;": "\xb8",
    "CenterDot;": "\xb7",
    "Cfr;": "\u212d",
    "Chi;": "\u03a7",
    "CircleDot;": "\u2299",
    "CircleMinus;": "\u2296",
    "CirclePlus;": "\u2295",
    "CircleTimes;": "\u2297",
    "ClockwiseContourIntegral;": "\u2232",
    "CloseCurlyDoubleQuote;": "\u201d",
    "CloseCurlyQuote;": "\u2019",
    "Colon;": "\u2237",
    "Colone;": "\u2a74",
    "Congruent;": "\u2261",
    "Conint;": "\u222f",
    "ContourIntegral;": "\u222e",
    "Copf;": "\u2102",
    "Coproduct;": "\u2210",
    "CounterClockwiseContourIntegral;": "\u2233",
    "Cross;": "\u2a2f",
    "Cscr;": "\U0001d49e",
    "Cup;": "\u22d3",
    "CupCap;": "\u224d",
    "DD;": "\u2145",
    "DDotrahd;": "\u2911",
    "DJcy;": "\u0402",
    "DScy;": "\u0405",
    "DZcy;": "\u040f",
    "Dagger;": "\u2021",
    "Darr;": "\u21a1",
    "Dashv;": "\u2ae4",
    "Dcaron;": "\u010e",
    "Dcy;": "\u0414",
    "Del;": "\u2207",
    "Delta;": "\u0394",
    "Dfr;": "\U0001d507",
    "DiacriticalAcute;": "\xb4",
    "DiacriticalDot;": "\u02d9",
    "DiacriticalDoubleAcute;": "\u02dd",
    "DiacriticalGrave;": "`",
    "DiacriticalTilde;": "\u02dc",
    "Diamond;": "\u22c4",
    "DifferentialD;": "\u2146",
    "Dopf;": "\U0001d53b",
    "Dot;": "\xa8",
    "DotDot;": "\u20dc",
    "DotEqual;": "\u2250",
    "DoubleContourIntegral;": "\u222f",
    "DoubleDot;": "\xa8",
    "DoubleDownArrow;": "\u21d3",
    "DoubleLeftArrow;": "\u21d0",
    "DoubleLeftRightArrow;": "\u21d4",
    "DoubleLeftTee;": "\u2ae4",
    "DoubleLongLeftArrow;": "\u27f8",
    "DoubleLongLeftRightArrow;": "\u27fa",
    "DoubleLongRightArrow;": "\u27f9",
    "DoubleRightArrow;": "\u21d2",
    "DoubleRightTee;": "\u22a8",
    "DoubleUpArrow;": "\u21d1",
    "DoubleUpDownArrow;": "\u21d5",
    "DoubleVerticalBar;": "\u2225",
    "DownArrow;": "\u2193",
    "DownArrowBar;": "\u2913",
    "DownArrowUpArrow;": "\u21f5",
    "DownBreve;": "\u0311",
    "DownLeftRightVector;": "\u2950",
    "DownLeftTeeVector;": "\u295e",
    "DownLeftVector;": "\u21bd",
    "DownLeftVectorBar;": "\u2956",
    "DownRightTeeVector;": "\u295f",
    "DownRightVector;": "\u21c1",
    "DownRightVectorBar;": "\u2957",
    "DownTee;": "\u22a4",
    "DownTeeArrow;": "\u21a7",
    "Downarrow;": "\u21d3",
    "Dscr;": "\U0001d49f",
    "Dstrok;": "\u0110",
    "ENG;": "\u014a",
    "ETH": "\xd0",
    "ETH;": "\xd0",
    "Eacute": "\xc9",
    "Eacute;": "\xc9",
    "Ecaron;": "\u011a",
    "Ecirc": "\xca",
    "Ecirc;": "\xca",
    "Ecy;": "\u042d",
    "Edot;": "\u0116",
    "Efr;": "\U0001d508",
    "Egrave": "\xc8",
    "Egrave;": "\xc8",
    "Element;": "\u2208",
    "Emacr;": "\u0112",
    "EmptySmallSquare;": "\u25fb",
    "EmptyVerySmallSquare;": "\u25ab",
    "Eogon;": "\u0118",
    "Eopf;": "\U0001d53c",
    "Epsilon;": "\u0395",
    "Equal;": "\u2a75",
    "EqualTilde;": "\u2242",
    "Equilibrium;": "\u21cc",
    "Escr;": "\u2130",
    "Esim;": "\u2a73",
    "Eta;": "\u0397",
    "Euml": "\xcb",
    "Euml;": "\xcb",
    "Exists;": "\u2203",
    "ExponentialE;": "\u2147",
    "Fcy;": "\u0424",
    "Ffr;": "\U0001d509",
    "FilledSmallSquare;": "\u25fc",
    "FilledVerySmallSquare;": "\u25aa",
    "Fopf;": "\U0001d53d",
    "ForAll;": "\u2200",
    "Fouriertrf;": "\u2131",
    "Fscr;": "\u2131",
    "GJcy;": "\u0403",
    "GT": ">",
    "GT;": ">",
    "Gamma;": "\u0393",
    "Gammad;": "\u03dc",
    "Gbreve;": "\u011e",
    "Gcedil;": "\u0122",
    "Gcirc;": "\u011c",
    "Gcy;": "\u0413",
    "Gdot;": "\u0120",
    "Gfr;": "\U0001d50a",
    "Gg;": "\u22d9",
    "Gopf;": "\U0001d53e",
    "GreaterEqual;": "\u2265",
    "GreaterEqualLess;": "\u22db",
    "GreaterFullEqual;": "\u2267",
    "GreaterGreater;": "\u2aa2",
    "GreaterLess;": "\u2277",
    "GreaterSlantEqual;": "\u2a7e",
    "GreaterTilde;": "\u2273",
    "Gscr;": "\U0001d4a2",
    "Gt;": "\u226b",
    "HARDcy;": "\u042a",
    "Hacek;": "\u02c7",
    "Hat;": "^",
    "Hcirc;": "\u0124",
    "Hfr;": "\u210c",
    "HilbertSpace;": "\u210b",
    "Hopf;": "\u210d",
    "HorizontalLine;": "\u2500",
    "Hscr;": "\u210b",
    "Hstrok;": "\u0126",
    "HumpDownHump;": "\u224e",
    "HumpEqual;": "\u224f",
    "IEcy;": "\u0415",
    "IJlig;": "\u0132",
    "IOcy;": "\u0401",
    "Iacute": "\xcd",
    "Iacute;": "\xcd",
    "Icirc": "\xce",
    "Icirc;": "\xce",
    "Icy;": "\u0418",
    "Idot;": "\u0130",
    "Ifr;": "\u2111",
    "Igrave": "\xcc",
    "Igrave;": "\xcc",
    "Im;": "\u2111",
    "Imacr;": "\u012a",
    "ImaginaryI;": "\u2148",
    "Implies;": "\u21d2",
    "Int;": "\u222c",
    "Integral;": "\u222b",
    "Intersection;": "\u22c2",
    "InvisibleComma;": "\u2063",
    "InvisibleTimes;": "\u2062",
    "Iogon;": "\u012e",
    "Iopf;": "\U0001d540",
    "Iota;": "\u0399",
    "Iscr;": "\u2110",
    "Itilde;": "\u0128",
    "Iukcy;": "\u0406",
    "Iuml": "\xcf",
    "Iuml;": "\xcf",
    "Jcirc;": "\u0134",
    "Jcy;": "\u0419",
    "Jfr;": "\U0001d50d",
    "Jopf;": "\U0001d541",
    "Jscr;": "\U0001d4a5",
    "Jsercy;": "\u0408",
    "Jukcy;": "\u0404",
    "KHcy;": "\u0425",
    "KJcy;": "\u040c",
    "Kappa;": "\u039a",
    "Kcedil;": "\u0136",
    "Kcy;": "\u041a",
    "Kfr;": "\U0001d50e",
    "Kopf;": "\U0001d542",
    "Kscr;": "\U0001d4a6",
    "LJcy;": "\u0409",
    "LT": "<",
    "LT;": "<",
    "Lacute;": "\u0139",
    "Lambda;": "\u039b",
    "Lang;": "\u27ea",
    "Laplacetrf;": "\u2112",
    "Larr;": "\u219e",
    "Lcaron;": "\u013d",
    "Lcedil;": "\u013b",
    "Lcy;": "\u041b",
    "LeftAngleBracket;": "\u27e8",
    "LeftArrow;": "\u2190",
    "LeftArrowBar;": "\u21e4",
    "LeftArrowRightArrow;": "\u21c6",
    "LeftCeiling;": "\u2308",
    "LeftDoubleBracket;": "\u27e6",
    "LeftDownTeeVector;": "\u2961",
    "LeftDownVector;": "\u21c3",
    "LeftDownVectorBar;": "\u2959",
    "LeftFloor;": "\u230a",
    "LeftRightArrow;": "\u2194",
    "LeftRightVector;": "\u294e",
    "LeftTee;": "\u22a3",
    "LeftTeeArrow;": "\u21a4",
    "LeftTeeVector;": "\u295a",
    "LeftTriangle;": "\u22b2",
    "LeftTriangleBar;": "\u29cf",
    "LeftTriangleEqual;": "\u22b4",
    "LeftUpDownVector;": "\u2951",
    "LeftUpTeeVector;": "\u2960",
    "LeftUpVector;": "\u21bf",
    "LeftUpVectorBar;": "\u2958",
    "LeftVector;": "\u21bc",
    "LeftVectorBar;": "\u2952",
    "Leftarrow;": "\u21d0",
    "Leftrightarrow;": "\u21d4",
    "LessEqualGreater;": "\u22da",
    "LessFullEqual;": "\u2266",
    "LessGreater;": "\u2276",
    "LessLess;": "\u2aa1",
    "LessSlantEqual;": "\u2a7d",
    "LessTilde;": "\u2272",
    "Lfr;": "\U0001d50f",
    "Ll;": "\u22d8",
    "Lleftarrow;": "\u21da",
    "Lmidot;": "\u013f",
    "LongLeftArrow;": "\u27f5",
    "LongLeftRightArrow;": "\u27f7",
    "LongRightArrow;": "\u27f6",
    "Longleftarrow;": "\u27f8",
    "Longleftrightarrow;": "\u27fa",
    "Longrightarrow;": "\u27f9",
    "Lopf;": "\U0001d543",
    "LowerLeftArrow;": "\u2199",
    "LowerRightArrow;": "\u2198",
    "Lscr;": "\u2112",
    "Lsh;": "\u21b0",
    "Lstrok;": "\u0141",
    "Lt;": "\u226a",
    "Map;": "\u2905",
    "Mcy;": "\u041c",
    "MediumSpace;": "\u205f",
    "Mellintrf;": "\u2133",
    "Mfr;": "\U0001d510",
    "MinusPlus;": "\u2213",
    "Mopf;": "\U0001d544",
    "Mscr;": "\u2133",
    "Mu;": "\u039c",
    "NJcy;": "\u040a",
    "Nacute;": "\u0143",
    "Ncaron;": "\u0147",
    "Ncedil;": "\u0145",
    "Ncy;": "\u041d",
    "NegativeMediumSpace;": "\u200b",
    "NegativeThickSpace;": "\u200b",
    "NegativeThinSpace;": "\u200b",
    "NegativeVeryThinSpace;": "\u200b",
    "NestedGreaterGreater;": "\u226b",
    "NestedLessLess;": "\u226a",
    "NewLine;": "\n",
    "Nfr;": "\U0001d511",
    "NoBreak;": "\u2060",
    "NonBreakingSpace;": "\xa0",
    "Nopf;": "\u2115",
    "Not;": "\u2aec",
    "NotCongruent;": "\u2262",
    "NotCupCap;": "\u226d",
    "NotDoubleVerticalBar;": "\u2226",
    "NotElement;": "\u2209",
    "NotEqual;": "\u2260",
    "NotEqualTilde;": "\u2242\u0338",
    "NotExists;": "\u2204",
    "NotGreater;": "\u226f",
    "NotGreaterEqual;": "\u2271",
    "NotGreaterFullEqual;": "\u2267\u0338",
    "NotGreaterGreater;": "\u226b\u0338",
    "NotGreaterLess;": "\u2279",
    "NotGreaterSlantEqual;": "\u2a7e\u0338",
    "NotGreaterTilde;": "\u2275",
    "NotHumpDownHump;": "\u224e\u0338",
    "NotHumpEqual;": "\u224f\u0338",
    "NotLeftTriangle;": "\u22ea",
    "NotLeftTriangleBar;": "\u29cf\u0338",
    "NotLeftTriangleEqual;": "\u22ec",
    "NotLess;": "\u226e",
    "NotLessEqual;": "\u2270",
    "NotLessGreater;": "\u2278",
    "NotLessLess;": "\u226a\u0338",
    "NotLessSlantEqual;": "\u2a7d\u0338",
    "NotLessTilde;": "\u2274",
    "NotNestedGreaterGreater;": "\u2aa2\u0338",
    "NotNestedLessLess;": "\u2aa1\u0338",
    "NotPrecedes;": "\u2280",
    "NotPrecedesEqual;": "\u2aaf\u0338",
    "NotPrecedesSlantEqual;": "\u22e0",
    "NotReverseElement;": "\u220c",
    "NotRightTriangle;": "\u22eb",
    "NotRightTriangleBar;": "\u29d0\u0338",
    "NotRightTriangleEqual;": "\u22ed",
    "NotSquareSubset;": "\u228f\u0338",
    "NotSquareSubsetEqual;": "\u22e2",
    "NotSquareSuperset;": "\u2290\u0338",
    "NotSquareSupersetEqual;": "\u22e3",
    "NotSubset;": "\u2282\u20d2",
    "NotSubsetEqual;": "\u2288",
    "NotSucceeds;": "\u2281",
    "NotSucceedsEqual;": "\u2ab0\u0338",
    "NotSucceedsSlantEqual;": "\u22e1",
    "NotSucceedsTilde;": "\u227f\u0338",
    "NotSuperset;": "\u2283\u20d2",
    "NotSupersetEqual;": "\u2289",
    "NotTilde;": "\u2241",
    "NotTildeEqual;": "\u2244",
    "NotTildeFullEqual;": "\u2247",
    "NotTildeTilde;": "\u2249",
    "NotVerticalBar;": "\u2224",
    "Nscr;": "\U0001d4a9",
    "Ntilde": "\xd1",
    "Ntilde;": "\xd1",
    "Nu;": "\u039d",
    "OElig;": "\u0152",
    "Oacute": "\xd3",
    "Oacute;": "\xd3",
    "Ocirc": "\xd4",
    "Ocirc;": "\xd4",
    "Ocy;": "\u041e",
    "Odblac;": "\u0150",
    "Ofr;": "\U0001d512",
    "Ograve": "\xd2",
    "Ograve;": "\xd2",
    "Omacr;": "\u014c",
    "Omega;": "\u03a9",
    "Omicron;": "\u039f",
    "Oopf;": "\U0001d546",
    "OpenCurlyDoubleQuote;": "\u201c",
    "OpenCurlyQuote;": "\u2018",
    "Or;": "\u2a54",
    "Oscr;": "\U0001d4aa",
    "Oslash": "\xd8",
    "Oslash;": "\xd8",
    "Otilde": "\xd5",
    "Otilde;": "\xd5",
    "Otimes;": "\u2a37",
    "Ouml": "\xd6",
    "Ouml;": "\xd6",
    "OverBar;": "\u203e",
    "OverBrace;": "\u23de",
    "OverBracket;": "\u23b4",
    "OverParenthesis;": "\u23dc",
    "PartialD;": "\u2202",
    "Pcy;": "\u041f",
    "Pfr;": "\U0001d513",
    "Phi;": "\u03a6",
    "Pi;": "\u03a0",
    "PlusMinus;": "\xb1",
    "Poincareplane;": "\u210c",
    "Popf;": "\u2119",
    "Pr;": "\u2abb",
    "Precedes;": "\u227a",
    "PrecedesEqual;": "\u2aaf",
    "PrecedesSlantEqual;": "\u227c",
    "PrecedesTilde;": "\u227e",
    "Prime;": "\u2033",
    "Product;": "\u220f",
    "Proportion;": "\u2237",
    "Proportional;": "\u221d",
    "Pscr;": "\U0001d4ab",
    "Psi;": "\u03a8",
    "QUOT": "\"",
    "QUOT;": "\"",
    "Qfr;": "\U0001d514",
    "Qopf;": "\u211a",
    "Qscr;": "\U0001d4ac",
    "RBarr;": "\u2910",
    "REG": "\xae",
    "REG;": "\xae",
    "Racute;": "\u0154",
    "Rang;": "\u27eb",
    "Rarr;": "\u21a0",
    "Rarrtl;": "\u2916",
    "Rcaron;": "\u0158",
    "Rcedil;": "\u0156",
    "Rcy;": "\u0420",
    "Re;": "\u211c",
    "ReverseElement;": "\u220b",
    "ReverseEquilibrium;": "\u21cb",
    "ReverseUpEquilibrium;": "\u296f",
    "Rfr;": "\u211c",
    "Rho;": "\u03a1",
    "RightAngleBracket;": "\u27e9",
    "RightArrow;": "\u2192",
    "RightArrowBar;": "\u21e5",
    "RightArrowLeftArrow;": "\u21c4",
    "RightCeiling;": "\u2309",
    "RightDoubleBracket;": "\u27e7",
    "RightDownTeeVector;": "\u295d",
    "RightDownVector;": "\u21c2",
    "RightDownVectorBar;": "\u2955",
    "RightFloor;": "\u230b",
    "RightTee;": "\u22a2",
    "RightTeeArrow;": "\u21a6",
    "RightTeeVector;": "\u295b",
    "RightTriangle;": "\u22b3",
    "RightTriangleBar;": "\u29d0",
    "RightTriangleEqual;": "\u22b5",
    "RightUpDownVector;": "\u294f",
    "RightUpTeeVector;": "\u295c",
