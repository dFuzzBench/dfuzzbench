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
        while len(self.buffer[i]) < offset:
            offset -= len(self.buffer[i])
            i += 1
        self.position = [i, offset]

    def read(self, bytes):
        if not self.buffer:
            return self._readStream(bytes)
        elif (self.position[0] == len(self.buffer) and
              self.position[1] == len(self.buffer[-1])):
            return self._readStream(bytes)
        else:
            return self._readFromBuffer(bytes)

    def _bufferedBytes(self):
        return sum([len(item) for item in self.buffer])

    def _readStream(self, bytes):
        data = self.stream.read(bytes)
        self.buffer.append(data)
        self.position[0] += 1
        self.position[1] = len(data)
        return data

    def _readFromBuffer(self, bytes):
        remainingBytes = bytes
        rv = []
        bufferIndex = self.position[0]
        bufferOffset = self.position[1]
        while bufferIndex < len(self.buffer) and remainingBytes != 0:
            assert remainingBytes > 0
            bufferedData = self.buffer[bufferIndex]

            if remainingBytes <= len(bufferedData) - bufferOffset:
                bytesToRead = remainingBytes
                self.position = [bufferIndex, bufferOffset + bytesToRead]
            else:
                bytesToRead = len(bufferedData) - bufferOffset
                self.position = [bufferIndex, len(bufferedData)]
                bufferIndex += 1
            rv.append(bufferedData[bufferOffset:bufferOffset + bytesToRead])
            remainingBytes -= bytesToRead

            bufferOffset = 0

        if remainingBytes:
            rv.append(self._readStream(remainingBytes))

        return b"".join(rv)


def HTMLInputStream(source, **kwargs):
    # Work around Python bug #20007: read(0) closes the connection.
    # http://bugs.python.org/issue20007
    if (isinstance(source, http_client.HTTPResponse) or
        # Also check for addinfourl wrapping HTTPResponse
        (isinstance(source, urllib.response.addbase) and
         isinstance(source.fp, http_client.HTTPResponse))):
        isUnicode = False
    elif hasattr(source, "read"):
        isUnicode = isinstance(source.read(0), text_type)
    else:
        isUnicode = isinstance(source, text_type)

    if isUnicode:
        encodings = [x for x in kwargs if x.endswith("_encoding")]
        if encodings:
            raise TypeError("Cannot set an encoding with a unicode input, set %r" % encodings)

        return HTMLUnicodeInputStream(source, **kwargs)
    else:
        return HTMLBinaryInputStream(source, **kwargs)


class HTMLUnicodeInputStream(object):
    """Provides a unicode stream of characters to the HTMLTokenizer.

    This class takes care of character encoding and removing or replacing
    incorrect byte-sequences and also provides column and line tracking.

    """

    _defaultChunkSize = 10240

    def __init__(self, source, **kwargs):
        """Initialises the HTMLInputStream.

        HTMLInputStream(source, [encoding]) -> Normalized stream from source
        for use by html5lib.

        source can be either a file-object, local filename or a string.

        The optional encoding parameter must be a string that indicates
        the encoding.  If specified, that encoding will be used,
        regardless of any BOM or later declaration (such as in a meta
        element)

        """

        if source is None:
            from io import StringIO
            source = StringIO("")

        if not _utils.supports_lone_surrogates:
            # Such platforms will have already checked for such
