"""Deprecated from html5lib 1.1.

See `here <https://github.com/html5lib/html5lib-python/issues/443>`_ for
information about its deprecation; `Bleach <https://github.com/mozilla/bleach>`_
is recommended as a replacement. Please let us know in the aforementioned issue
if Bleach is unsuitable for your needs.

"""
from __future__ import absolute_import, division, unicode_literals

import re
import warnings
from xml.sax.saxutils import escape, unescape

from six.moves import urllib_parse as urlparse

from . import base
from ..constants import namespaces, prefixes

__all__ = ["Filter"]


_deprecation_msg = (
    "html5lib's sanitizer is deprecated; see " +
    "https://github.com/html5lib/html5lib-python/issues/443 and please let " +
    "us know if Bleach is unsuitable for your needs"
)

warnings.warn(_deprecation_msg, DeprecationWarning)

allowed_elements = frozenset((
    (namespaces['html'], 'a'),
    (namespaces['html'], 'abbr'),
    (namespaces['html'], 'acronym'),
    (namespaces['html'], 'address'),
    (namespaces['html'], 'area'),
    (namespaces['html'], 'article'),
    (namespaces['html'], 'aside'),
    (namespaces['html'], 'audio'),
    (namespaces['html'], 'b'),
    (namespaces['html'], 'big'),
    (namespaces['html'], 'blockquote'),
    (namespaces['html'], 'br'),
    (namespaces['html'], 'button'),
    (namespaces['html'], 'canvas'),
    (namespaces['html'], 'caption'),
    (namespaces['html'], 'center'),
    (namespaces['html'], 'cite'),
    (namespaces['html'], 'code'),
    (namespaces['html'], 'col'),
    (namespaces['html'], 'colgroup'),
    (namespaces['html'], 'command'),
    (namespaces['html'], 'datagrid'),
    (namespaces['html'], 'datalist'),
    (namespaces['html'], 'dd'),
    (namespaces['html'], 'del'),
    (namespaces['html'], 'details'),
    (namespaces['html'], 'dfn'),
    (namespaces['html'], 'dialog'),
    (namespaces['html'], 'dir'),
    (namespaces['html'], 'div'),
    (namespaces['html'], 'dl'),
    (namespaces['html'], 'dt'),
    (namespaces['html'], 'em'),
    (namespaces['html'], 'event-source'),
    (namespaces['html'], 'fieldset'),
    (namespaces['html'], 'figcaption'),
    (namespaces['html'], 'figure'),
    (namespaces['html'], 'footer'),
    (namespaces['html'], 'font'),
    (namespaces['html'], 'form'),
    (namespaces['html'], 'header'),
    (namespaces['html'], 'h1'),
    (namespaces['html'], 'h2'),
    (namespaces['html'], 'h3'),
    (namespaces['html'], 'h4'),
    (namespaces['html'], 'h5'),
    (namespaces['html'], 'h6'),
    (namespaces['html'], 'hr'),
    (namespaces['html'], 'i'),
    (namespaces['html'], 'img'),
    (namespaces['html'], 'input'),
    (namespaces['html'], 'ins'),
    (namespaces['html'], 'keygen'),
    (namespaces['html'], 'kbd'),
    (namespaces['html'], 'label'),
    (namespaces['html'], 'legend'),
    (namespaces['html'], 'li'),
    (namespaces['html'], 'm'),
    (namespaces['html'], 'map'),
    (namespaces['html'], 'menu'),
    (namespaces['html'], 'meter'),
    (namespaces['html'], 'multicol'),
    (namespaces['html'], 'nav'),
    (namespaces['html'], 'nextid'),
    (namespaces['html'], 'ol'),
    (namespaces['html'], 'output'),
    (namespaces['html'], 'optgroup'),
    (namespaces['html'], 'option'),
    (namespaces['html'], 'p'),
    (namespaces['html'], 'pre'),
    (namespaces['html'], 'progress'),
    (namespaces['html'], 'q'),
    (namespaces['html'], 's'),
    (namespaces['html'], 'samp'),
    (namespaces['html'], 'section'),
    (namespaces['html'], 'select'),
    (namespaces['html'], 'small'),
    (namespaces['html'], 'sound'),
    (namespaces['html'], 'source'),
    (namespaces['html'], 'spacer'),
    (namespaces['html'], 'span'),
    (namespaces['html'], 'strike'),
    (namespaces['html'], 'strong'),
    (namespaces['html'], 'sub'),
    (namespaces['html'], 'sup'),
    (namespaces['html'], 'table'),
    (namespaces['html'], 'tbody'),
    (namespaces['html'], 'td'),
    (namespaces['html'], 'textarea'),
    (namespaces['html'], 'time'),
    (namespaces['html'], 'tfoot'),
    (namespaces['html'], 'th'),
    (namespaces['html'], 'thead'),
    (namespaces['html'], 'tr'),
    (namespaces['html'], 'tt'),
    (namespaces['html'], 'u'),
    (namespaces['html'], 'ul'),
    (namespaces['html'], 'var'),
    (namespaces['html'], 'video'),
    (namespaces['mathml'], 'maction'),
    (namespaces['mathml'], 'math'),
    (namespaces['mathml'], 'merror'),
    (namespaces['mathml'], 'mfrac'),
    (namespaces['mathml'], 'mi'),
    (namespaces['mathml'], 'mmultiscripts'),
    (namespaces['mathml'], 'mn'),
    (namespaces['mathml'], 'mo'),
    (namespaces['mathml'], 'mover'),
    (namespaces['mathml'], 'mpadded'),
    (namespaces['mathml'], 'mphantom'),
