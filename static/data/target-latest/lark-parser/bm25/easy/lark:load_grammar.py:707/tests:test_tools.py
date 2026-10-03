from __future__ import absolute_import, print_function

from unittest import TestCase, main

from lark import Lark
from lark.tree import Tree
from lark.tools import standalone

from io import StringIO


class TestStandalone(TestCase):
    def setUp(self):
        pass

    def _create_standalone(self, grammar, compress=False):
        code_buf = StringIO()
        standalone.gen_standalone(Lark(grammar, parser='lalr'), out=code_buf, compress=compress)
        code = code_buf.getvalue()

        context = {'__doc__': None, '__name__': 'test_standalone'}
        exec(code, context)
        return context

    def test_simple(self):
        grammar = """
            start: NUMBER WORD

            %import common.NUMBER
            %import common.WORD
            %import common.WS
            %ignore WS

        """

        context = self._create_standalone(grammar)

        _Lark = context['Lark_StandAlone']
        l = _Lark()
        x = l.parse('12 elephants')
        self.assertEqual(x.children, ['12', 'elephants'])
        x = l.parse('16 candles')
        self.assertEqual(x.children, ['16', 'candles'])

        self.assertRaises(context['UnexpectedToken'], l.parse, 'twelve monkeys')
        self.assertRaises(context['UnexpectedToken'], l.parse, 'twelve')
        self.assertRaises(context['UnexpectedCharacters'], l.parse, '$ talks')

        context = self._create_standalone(grammar, compress=True)
        _Lark = context['Lark_StandAlone']
        l = _Lark()
        x = l.parse('12 elephants')

    def test_interactive(self):
        grammar = """
                start: A+ B*
