from __future__ import absolute_import, division, unicode_literals

from types import ModuleType

try:
    from collections.abc import Mapping
except ImportError:
    from collections import Mapping

from six import text_type, PY3

if PY3:
    import xml.etree.ElementTree as default_etree
else:
    try:
        import xml.etree.cElementTree as default_etree
    except ImportError:
        import xml.etree.ElementTree as default_etree


__all__ = ["default_etree", "MethodDispatcher", "isSurrogatePair",
           "surrogatePairToCodepoint", "moduleFactoryFactory",
           "supports_lone_surrogates"]


# Platforms not supporting lone surrogates (\uD800-\uDFFF) should be
# caught by the below test. In general this would be any platform
# using UTF-16 as its encoding of unicode strings, such as
# Jython. This is because UTF-16 itself is based on the use of such
# surrogates, and there is no mechanism to further escape such
# escapes.
try:
    _x = eval('"\\uD800"')  # pylint:disable=eval-used
    if not isinstance(_x, text_type):
        # We need this with u"" because of http://bugs.jython.org/issue2039
        _x = eval('u"\\uD800"')  # pylint:disable=eval-used
        assert isinstance(_x, text_type)
except Exception:
    supports_lone_surrogates = False
else:
    supports_lone_surrogates = True


class MethodDispatcher(dict):
    """Dict with 2 special properties:

    On initiation, keys that are lists, sets or tuples are converted to
    multiple keys so accessing any one of the items in the original
    list-like object returns the matching value

    md = MethodDispatcher({("foo", "bar"):"baz"})
    md["foo"] == "baz"

    A default value which can be set through the default attribute.
    """

    def __init__(self, items=()):
        _dictEntries = []
        for name, value in items:
            if isinstance(name, (list, tuple, frozenset, set)):
                for item in name:
                    _dictEntries.append((item, value))
            else:
                _dictEntries.append((name, value))
        dict.__init__(self, _dictEntries)
        assert len(self) == len(_dictEntries)
        self.default = None

