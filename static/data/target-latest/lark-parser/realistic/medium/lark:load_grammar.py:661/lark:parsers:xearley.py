"""This module implements an Earley parser with a dynamic lexer

The core Earley algorithm used here is based on Elizabeth Scott's implementation, here:
    https://www.sciencedirect.com/science/article/pii/S1571066108001497

That is probably the best reference for understanding the algorithm here.

The Earley parser outputs an SPPF-tree as per that document. The SPPF tree format
is better documented here:
    http://www.bramvandersanden.com/post/2014/06/shared-packed-parse-forest/

Instead of running a lexer beforehand, or using a costy char-by-char method, this parser
uses regular expressions by necessity, achieving high-performance while maintaining all of
Earley's power in parsing any CFG.
"""

from typing import TYPE_CHECKING, Callable, Optional, List, Any
from collections import defaultdict

from ..tree import Tree
from ..exceptions import UnexpectedCharacters
from ..lexer import Token
from ..grammar import Terminal
from .earley import Parser as BaseParser
from .earley_forest import TokenNode

if TYPE_CHECKING:
    from ..common import LexerConf, ParserConf

class Parser(BaseParser):
    def __init__(self, lexer_conf: 'LexerConf', parser_conf: 'ParserConf', term_matcher: Callable,
                 resolve_ambiguity: bool=True, complete_lex: bool=False, debug: bool=False,
                 tree_class: Optional[Callable[[str, List], Any]]=Tree, ordered_sets: bool=True):
        BaseParser.__init__(self, lexer_conf, parser_conf, term_matcher, resolve_ambiguity,
                            debug, tree_class, ordered_sets)
        self.ignore = [Terminal(t) for t in lexer_conf.ignore]
        self.complete_lex = complete_lex

    def _parse(self, stream, columns, to_scan, start_symbol=None):

        def scan(i, to_scan):
            """The core Earley Scanner.

            This is a custom implementation of the scanner that uses the
            Lark lexer to match tokens. The scan list is built by the
            Earley predictor, based on the previously completed tokens.
            This ensures that at each phase of the parse we have a custom
            lexer context, allowing for more complex ambiguities."""

            node_cache = {}

            # 1) Loop the expectations and ask the lexer to match.
            # Since regexp is forward looking on the input stream, and we only
            # want to process tokens when we hit the point in the stream at which
            # they complete, we push all tokens into a buffer (delayed_matches), to
            # be held possibly for a later parse step when we reach the point in the
            # input stream at which they complete.
            for item in self.Set(to_scan):
                m = match(item.expect, stream, i)
                if m:
                    t = Token(item.expect.name, m.group(0), i, text_line, text_column)
                    delayed_matches[m.end()].append( (item, i, t) )

                    if self.complete_lex:
                        s = m.group(0)
                        for j in range(1, len(s)):
                            m = match(item.expect, s[:-j])
                            if m:
                                t = Token(item.expect.name, m.group(0), i, text_line, text_column)
                                delayed_matches[i+m.end()].append( (item, i, t) )

                    # XXX The following 3 lines were commented out for causing a bug. See issue #768
                    # # Remove any items that successfully matched in this pass from the to_scan buffer.
                    # # This ensures we don't carry over tokens that already matched, if we're ignoring below.
                    # to_scan.remove(item)

            # 3) Process any ignores. This is typically used for e.g. whitespace.
            # We carry over any unmatched items from the to_scan buffer to be matched again after
            # the ignore. This should allow us to use ignored symbols in non-terminals to implement
            # e.g. mandatory spacing.
            for x in self.ignore:
                m = match(x, stream, i)
                if m:
                    # Carry over any items still in the scan buffer, to past the end of the ignored items.
                    delayed_matches[m.end()].extend([(item, i, None) for item in to_scan ])

                    # If we're ignoring up to the end of the file, # carry over the start symbol if it already completed.
                    delayed_matches[m.end()].extend([(item, i, None) for item in columns[i] if item.is_complete and item.s == start_symbol])

            next_to_scan = self.Set()
            next_set = self.Set()
            columns.append(next_set)
            transitives.append({})

            ## 4) Process Tokens from delayed_matches.
            # This is the core of the Earley scanner. Create an SPPF node for each Token,
            # and create the symbol node in the SPPF tree. Advance the item that completed,
            # and add the resulting new item to either the Earley set (for processing by the
            # completer/predictor) or the to_scan buffer for the next parse step.
            for item, start, token in delayed_matches[i+