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
                    Tree('a', [Tree('b