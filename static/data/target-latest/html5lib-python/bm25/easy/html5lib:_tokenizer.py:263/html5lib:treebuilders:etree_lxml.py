"""Module for supporting the lxml.etree library. The idea here is to use as much
of the native library as possible, without using fragile hacks like custom element
names that break between releases. The downside of this is that we cannot represent
all possible trees; specifically the following are known to cause problems:

Text or comments as siblings of the root element
Docypes with no name

When any of these things occur, we emit a DataLossWarning
"""

from __future__ import absolute_import, division, unicode_literals
# pylint:disable=protected-access

import warnings
import re
import sys

try:
    from collections.abc import MutableMapping
except ImportError:
    from collections import MutableMapping

from . import base
from ..constants import DataLossWarning
from .. import constants
from . import etree as etree_builders
from .. import _ihatexml

import lxml.etree as etree
from six import PY3, binary_type


fullTree = True
tag_regexp = re.compile("{([^}]*)}(.*)")

comment_type = etree.Comment("asd").tag


class DocumentType(object):
    def __init__(self, name, publicId, systemId):
        self.name = name
        self.publicId = publicId
        self.systemId = systemId


class Document(object):
    def __init__(self):
        self._elementTree = None
        self._childNodes = []

    def appendChild(self, element):
        last = self._elementTree.getroot()
        for last in self._elementTree.getroot().itersiblings():
            pass

        last.addnext(element._element)

    def _getChildNodes(self):
        return self._childNodes

    childNodes = property(_getChildNodes)


def testSerializer(element):
    rv = []
    infosetFilter = _ihatexml.InfosetFilter(preventDoubleDashComments=True)

    def serializeElement(element, indent=0):
