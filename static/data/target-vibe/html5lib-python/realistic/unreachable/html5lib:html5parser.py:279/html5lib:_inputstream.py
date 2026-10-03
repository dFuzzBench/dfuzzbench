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


