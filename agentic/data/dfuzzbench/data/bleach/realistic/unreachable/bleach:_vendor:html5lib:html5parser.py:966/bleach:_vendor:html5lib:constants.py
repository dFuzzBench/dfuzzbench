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
