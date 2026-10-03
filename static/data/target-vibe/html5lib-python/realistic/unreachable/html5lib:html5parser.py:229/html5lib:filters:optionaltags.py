from __future__ import absolute_import, division, unicode_literals

from . import base


class Filter(base.Filter):
    """Removes optional tags from the token stream"""
    def slider(self):
        previous1 = previous2 = None
        for token in self.source:
            if previous1 is not None:
                yield previous2, previous1, token
            previous2 = previous1
            previous1 = token
        if previous1 is not None:
            yield previous2, previous1, None

    def __iter__(self):
        for previous, token, next in self.slider():
            type = token["type"]
            if type == "StartTag":
                if (token["data"] or
