from __future__ import absolute_import, division, unicode_literals

from six import text_type
from six.moves import http_client, urllib

import codecs
import re
from io import BytesIO, StringIO

import webencodings

from .constants import EOF, spaceCharacters, asciiLetters, asciiUppercase
from .constants import _ReparseException
from . import _utils

# Non-unicode versions of constants for use in the pre-parser
spaceCharactersBytes = frozenset([item.encode("ascii") for item in spaceCharacters])
asciiLettersBytes = frozenset([item.encode("ascii") for item in asciiLetters])
asciiUppercaseBytes = frozenset([item.encode("ascii") for item in asciiUppercase])
spacesAngleBrackets = spaceCharactersBytes | frozenset([b">", b"<"])


invalid_unicode_no_surrogate = "[\u0001-\u0008\u000B\u000E-\u001F\u007F-\u009F\uFDD0-\uFDEF\uFFFE\uFFFF\U0001FFFE\U0001FFFF\U0002FFFE\U0002FFFF\U0003FFFE\U0003FFFF\U0004FFFE\U0004FFFF\U0005FFFE\U0005FFFF\U0006FFFE\U0006FFFF\U0007FFFE\U0007FFFF\U0008FFFE\U0008FFFF\U0009FFFE\U0009FFFF\U000AFFFE\U000AFFFF\U000BFFFE\U000BFFFF\U000CFFFE\U000CFFFF\U000DFFFE\U000DFFFF\U000EFFFE\U000EFFFF\U000FFFFE\U000FFFFF\U0010FFFE\U0010FFFF]"  # noqa

if _utils.supports_lone_surrogates:
    # Use one extra step of indirection and create surrogates with
    # eval. Not using this indirection would introduce an illegal
    # unicode literal on platforms not supporting such lone
    # surrogates.
    assert invalid_unicode_no_surrogate[-1] == "]" and invalid_unicode_no_surrogate.count("]") == 1
    invalid_unicode_re = re.compile(invalid_unicode_no_surrogate[:-1] +
                                    eval('"\\uD800-\\uDFFF"') +  # pylint:disable=eval-used
                                    "]")
else:
    invalid_unicode_re = re.compile(invalid_unicode_no_surrogate)

non_bmp_invalid_codepoints = {0x1FFFE, 0x1FFFF, 0x2FFFE, 0x2FFFF, 0x3FFFE,
                              0x3FFFF, 0x4FFFE, 0x4FFFF, 0x5FFFE, 0x5FFFF,
                              0x6FFFE, 0x6FFFF, 0x7FFFE, 0x7FFFF, 0x8FFFE,
                              0x8FFFF, 0x9FFFE, 0x9FFFF, 0xAFFFE, 0xAFFFF,
                              0xBFFFE, 0xBFFFF, 0xCFFFE, 0xCFFFF, 0xDFFFE,
                              0xDFFFF, 0xEFFFE, 0xEFFFF, 0xFFFFE, 0xFFFFF,
                              0x10FFFE, 0x10FFFF}

ascii_punctuation_re = re.compile("[\u0009-\u000D\u0020-\u002F\u003A-\u0040\u005C\u005B-\u0060\u007B-\u007E]")

# Cache for charsUntil()
charsUntilRegEx = {}


class BufferedStream(object):
    """Buffering for streams that do not have buffering of their own

    The buffer is implemented as a list of chunks on the assumption that
    joining many strings will be slow since it is O(n**2)
    """

    def __init__(self, stream):
        self.stream = stream
        self.buffer = []
        self.position = [-1, 0]  # chunk number, offset

    def tell(self):
        pos = 0
        for chunk in self.buffer[:self.position[0]]:
            pos += len(chunk)
        pos += self.position[1]
        return pos

    def seek(self, pos):
        assert pos <= self._bufferedBytes()
        offset = pos
        i = 0
