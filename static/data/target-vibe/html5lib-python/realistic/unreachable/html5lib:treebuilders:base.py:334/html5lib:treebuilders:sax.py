
from .base import TreeBuilder

class SAXTreeBuilder(TreeBuilder):
    def __init__(self, namespaceHTMLElements, content_handler):
        super(SAXTreeBuilder, self).__init__(namespaceHTMLElements)
        self.content_handler = content_handler

    def insertElementNormal(self, token):
        self.content_handler.startElement(token["name"], token["data"])
        return super(SAXTreeBuilder, self).insertElementNormal(token)

    def processEndTag(self, name):
        self.content_handler.endElement(name)

    def insertText(self, data, parent=None, location=None):
        self.content_handler.characters(data)
