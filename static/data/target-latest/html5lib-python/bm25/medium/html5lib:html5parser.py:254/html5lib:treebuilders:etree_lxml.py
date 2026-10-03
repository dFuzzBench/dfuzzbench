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
        if not hasattr(element, "tag"):
            if hasattr(element, "getroot"):
                # Full tree case
                rv.append("#document")
                if element.docinfo.internalDTD:
                    if not (element.docinfo.public_id or
                            element.docinfo.system_url):
                        dtd_str = "<!DOCTYPE %s>" % element.docinfo.root_name
                    else:
                        dtd_str = """<!DOCTYPE %s "%s" "%s">""" % (
                            element.docinfo.root_name,
                            element.docinfo.public_id,
                            element.docinfo.system_url)
                    rv.append("|%s%s" % (' ' * (indent + 2), dtd_str))
                next_element = element.getroot()
                while next_element.getprevious() is not None:
                    next_element = next_element.getprevious()
                while next_element is not None:
                    serializeElement(next_element, indent + 2)
                    next_element = next_element.getnext()
            elif isinstance(element, str) or isinstance(element, bytes):
                # Text in a fragment
                assert isinstance(element, str) or sys.version_info[0] == 2
                rv.append("|%s\"%s\"" % (' ' * indent, element))
            else:
                # Fragment case
                rv.append("#document-fragment")
                for next_element in element:
                    serializeElement(next_element, indent + 2)
        elif element.tag == comment_type:
            rv.append("|%s<!-- %s -->" % (' ' * indent, element.text))
            if hasattr(element, "tail") and element.tail:
                rv.append("|%s\"%s\"" % (' ' * indent, element.tail))
        else:
            assert isinstance(element, etree._Element)
            nsmatch = etree_builders.tag_regexp.match(element.tag)
            if nsmatch is not None:
                ns = nsmatch.group(1)
                tag = nsmatch.group(2)
                prefix = constants.prefixes[ns]
                rv.append("|%s<%s %s>" % (' ' * indent, prefix,
                                          infosetFilter.fromXmlName(tag)))
            else:
                rv.append("|%s<%s>" % (' ' * indent,
                                       infosetFilter.fromXmlName(element.tag)))

            if hasattr(element, "attrib"):
                attributes = []
                for name, value in element.attrib.items():
                    nsmatch = tag_regexp.match(name)
                    if nsmatch is not None:
                        ns, name = nsmatch.groups()
                        name = infosetFilter.fromXmlName(name)
                        prefix = constants.prefixes[ns]
                        attr_string = "%s %s" % (prefix, name)
                    else:
                        attr_string = infosetFilter.fromXmlName(name)
                    attributes.append((attr_string, value))

                for name, value in sorted(attributes):
                    rv.append('|%s%s="%s"' % (' ' * (indent + 2), name, value))

            if element.text:
                rv.append("|%s\"%s\"" % (' ' * (indent + 2), element.text))
            indent += 2
            for child in element:
                serializeElement(child, indent)
            if hasattr(element, "tail") and element.tail:
                rv.append("|%s\"%s\"" % (' ' * (indent - 2), element.tail))
    serializeElement(element, 0)

    return "\n".join(rv)


def tostring(element):
    """Serialize an element and its child nodes to a string"""
    rv = []

    def serializeElement(element):
        if not hasattr(element, "tag"):
            if element.docinfo.internalDTD:
                if element.docinfo.doctype:
                    dtd_str = element.docinfo.doctype
                else:
                    dtd_str = "<!DOCTYPE %s>" % element.docinfo.root_name
                rv.append(dtd_str)
            serializeElement(element.getroot())

        elif element.tag == comment_type:
            rv.append("<!--%s-->" % (element.text,))

        else:
            # This is assumed to be an ordinary element
            if not element.attrib:
                rv.append("<%s>" % (element.tag,))
            else:
                attr = " ".join(["%s=\"%s\"" % (name, value)
                                 for name, value in element.attrib.items()])
                rv.append("<%s %s>" % (element.tag, attr))
            if element.text:
                rv.append(element.text)

            for child in element:
                serializeElement(child)

            rv.append("</%s>" % (element.tag,))

        if hasattr(element, "tail") and element.tail:
            rv.append(element.tail)

    serializeElement(element)

    return "".join(rv)


class TreeBuilder(base.TreeBuilder):
    documentClass = Document
    doctypeClass = DocumentType
    elementClass = None
    commentClass = None
    fragmentClass = Document
    implementation = etree

    def __init__(self, namespaceHTMLElements, fullTree=False):
        builder = etree_builders.getETreeModule(etree, fullTree=fullTree)
        infosetFilter