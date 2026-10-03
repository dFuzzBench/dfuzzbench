from __future__ import absolute_import

import unittest
from functools import partial, reduce, partialmethod
from operator import add, mul
from unittest import TestCase
import copy
import pickle
import functools

from lark.tree import Tree
from lark.lexer import Token
from lark.visitors import Visitor, Visitor_Recursive, Transformer, Interpreter, visit_children_decor, v_args, Discard, Transformer_InPlace, \
    Transformer_InPlaceRecursive, Transformer_NonRecursive, merge_transformers


class TestTrees(TestCase):
    def setUp(self):
        self.tree1 = Tree('a', [Tree(x, y) for x, y in zip('bcd', 'xyz')])

    def test_eq(self):
        assert self.tree1 == self.tree1
        assert self.tree1 != 0

    def test_copy(self):
        assert self.tree1 == copy.copy(self.tree1)

    def test_deepcopy(self):
        assert self.tree1 == copy.deepcopy(self.tree1)

    def test_pickle(self):
        s = copy.deepcopy(self.tree1)
        data = pickle.dumps(s, protocol=pickle.HIGHEST_PROTOCOL)
        assert pickle.loads(data) == s

    def test_repr_runnable(self):
        assert self.tree1 == eval(repr(self.tree1))

    def test_iter_subtrees(self):
        expected = [Tree('b', 'x'), Tree('c', 'y'), Tree('d', 'z'),
                    Tree('a', [Tree('b', 'x'), Tree('c', 'y'), Tree('d', 'z')])]
        nodes = list(self.tree1.iter_subtrees())
        self.assertEqual(nodes, expected)

    def test_iter_subtrees_topdown(self):
        expected = [Tree('a', [Tree('b', 'x'), Tree('c', 'y'), Tree('d', 'z')]),
                    Tree('b', 'x'), Tree('c', 'y'), Tree('d', 'z')]
        nodes = list(self.tree1.iter_subtrees_topdown())
        self.assertEqual(nodes, expected)

    def test_visitor(self):
        class Visitor1(Visitor):
            def __init__(self):
                self.nodes=[]

            def __default__(self,tree):
                self.nodes.append(tree)
        class Visitor1_Recursive(Visitor_Recursive):
            def __init__(self):
                self.nodes=[]

            def __default__(self,tree):
                self.nodes.append(tree)

        visitor1=Visitor1()
        visitor1_recursive=Visitor1_Recursive()

        expected_top_down = [Tree('a', [Tree('b', 'x'), Tree('c', 'y'), Tree('d', 'z')]),
                    Tree('b', 'x'), Tree('c', 'y'), Tree('d', 'z')]
        expected_botton_up= [Tree('b', 'x'), Tree('c', 'y'), Tree('d', 'z'),
                    Tree('a', [Tree('b', 'x'), Tree('c', 'y'), Tree('d', 'z')])]

        visitor1.visit(self.tree1)
        self.assertEqual(visitor1.nodes,expected_botton_up)

        visitor1_recursive.visit(self.tree1)
        self.assertEqual(visitor1_recursive.nodes,expected_botton_up)

        visitor1.nodes=[]
        visitor1_recursive.nodes=[]

        visitor1.visit_topdown(self.tree1)
        self.assertEqual(visitor1.nodes,expected_top_down)

        visitor1_recursive.visit_topdown(self.tree1)
        self.assertEqual(visitor1_recursive.nodes,expected_top_down)

    def test_interp(self):
        t = Tree('a', [Tree('b', []), Tree('c', []), 'd'])

        class Interp1(Interpreter):
            def a(self, tree):
                return self.visit_children(tree) + ['e']

            def b(self, tree):
                return 'B'

            def c(self, tree):
                return 'C'

        self.assertEqual(Interp1().visit(t), list('BCde'))

        class Interp2(Interpreter):
            @visit_children_decor
            def a(self, values):
                return values + ['e']

            def b(self, tree):
                return 'B'

            def c(self, tree):
                return 'C'

        self.assertEqual(Interp2().visit(t