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
    338,    # 0x8C  0x0152  LATIN