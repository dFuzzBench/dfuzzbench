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

        tree = TreeForestTransformer(resolve_ambiguity=False, tree_class=CustomTree).transform(self.forest)
        TreeChecker().visit(tree)

    def test_token_calls(self):

        visited = [False] * 4

        class CustomTransformer(TreeForestTransformer):
            def A(self, node):
                assert node.type == 'A'
                visited[0] = True
            def B(self, node):
                assert node.type == 'B'
                visited[1] = True
            def C(self, node):
                assert node.type == 'C'
                visited[2] = True
            def D(self, node):
                assert node.type == 'D'
                visited[3] = True

        tree = CustomTransformer(resolve_ambiguity=False).transform(self.forest)
        assert visited == [True] * 4

    def test_default_token(self):

        token_count = [0]

        class CustomTransformer(TreeForestTransformer):
            def __default_token__(self, node):
                token_count[0] += 1
                assert isinstance(node, Token)

        tree = CustomTransformer(resolve_ambiguity=True).transform(self.forest)
        self.assertEqual(token_count[0], 4)

    def test_rule_calls(self):

        visited_start = [False]
        visited_ab = [False]
        visited_bc = [False]
        visited_cd = [False]

        class CustomTransformer(TreeForestTransformer):
            def start(self, data):
                visited_start[0] = True
            def ab(self, data):
                visited_ab[0] = True
            def bc(self, data):
                visited_bc[0] = True
            def cd(self, data):
                visited_cd[0] = True

        tree = CustomTransformer(resolve_ambiguity=False).transform(self.forest)
        self.assertTrue(visited_start[0])
        self.assertTrue(visited_ab[0])
        self.assertTrue(visited_bc[0])
        self.assertTrue(visited_cd[0])

    def test_default_rule(self):

        rule_count = [0]

        class CustomTransformer(TreeForestTransformer):
            def __default__(self, name, data):
                rule_count[0] += 1

        tree = CustomTransformer(resolve_ambiguity=True).