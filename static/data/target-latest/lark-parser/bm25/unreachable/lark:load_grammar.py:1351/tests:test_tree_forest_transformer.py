from __future__ import absolute_import

import unittest

from lark import Lark
from lark.lexer import Token
from lark.tree import Tree
from lark.visitors import Visitor, Transformer, Discard
from lark.parsers.earley_forest import TreeForestTransformer, handles_ambiguity

class TestTreeForestTransformer(unittest.TestCase):

    grammar = """
    start: ab bc cd
    !ab: "A" "B"?
    !bc: "B"? "C"?
    !cd: "C"? "D"
    """

    parser = Lark(grammar, parser='earley', ambiguity='forest')
    forest = parser.parse("ABCD")

    def test_identity_resolve_ambiguity(self):
        l = Lark(self.grammar, parser='earley', ambiguity='resolve')
        tree1 = l.parse("ABCD")
        tree2 = TreeForestTransformer(resolve_ambiguity=True).transform(self.forest)
        self.assertEqual(tree1, tree2)

    def test_identity_explicit_ambiguity(self):
        l = Lark(self.grammar, parser='earley', ambiguity='explicit')
        tree1 = l.parse("ABCD")
        tree2 = TreeForestTransformer(resolve_ambiguity=False).transform(self.forest)
        self.assertEqual(tree1, tree2)

    def test_tree_class(self):

        class CustomTree(Tree):
            pass

        class TreeChecker(Visitor):
            def __default__(self, tree):
                assert isinstance(tree, CustomTree)

        tree = TreeForestTransformer(resolve_ambiguity=False, tree_class