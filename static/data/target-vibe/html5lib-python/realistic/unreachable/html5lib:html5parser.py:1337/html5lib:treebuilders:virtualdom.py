
from .base import TreeBuilder

class VirtualDOMTreeBuilder(TreeBuilder):
    def insertElementNormal(self, token):
        node = {'tag': token["name"], 'attrs': token["data"], 'children': []}
        if self.openElements:
            self.openElements[-1]['children'].append(node)
        self.openElements.append(node)
        return node

    def insertText(self, data, parent=None, location=None):
        if self.openElements:
            self.openElements[-1]['children'].append(data)
