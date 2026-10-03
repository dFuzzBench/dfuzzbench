import os

import pytest

from bleach import clean
from bleach.html5lib_shim import Filter
from bleach.sanitizer import ALLOWED_PROTOCOLS, Cleaner, NoCssSanitizerWarning
from bleach._vendor.html5lib.constants import rcdataElements


@pytest.mark.parametrize(
    "data",
    [
        "a < b",
        "link http://link.com",
        "text<em>",
        # Verify idempotentcy with character entity handling
        "<span>text & </span>",
        "jim &current joe",
        "&&nbsp; &nbsp;&",
        "jim &xx; joe",
        # Link with querystring items
        '<a href="http://example.com?foo=bar&bar=foo&amp;biz=bash">',
    ],
)
def test_clean_idempotent(data):
    """Make sure that applying the filter twice doesn't change anything."""
    assert clean(clean(data)) == clean(data)


def test_clean_idempotent_img():
    tags = {"img"}
    dirty = '<imr src="http://example.com?foo=bar&bar=foo&amp;biz=bash">'
    assert clean(clean(dirty, tags=tags), tags=tags) == clean(dirty, tags=tags)


def test_only_text_is_cleaned():
    some_text = "text"
    some_type = int
    no_type = None

    assert clean(some_text) == some_text

    with pytest.raises(TypeError) as e:
        clean(some_type)
    assert "argument cannot be of 'type' type" in str(e.value)

    with pytest.raises(TypeError) as e:
        clean(no_type)
    assert "NoneType" in str(e.value)


def test_empty():
    assert clean("") == ""


def test_content_has_no_html():
    assert clean("no html string") == "no html string"


@pytest.mark.parametrize(
    "data, expected",
    [
        ("an <strong>allowed</strong> tag", "an <strong>allowed</strong> tag"),
        ("another <em>good</em> tag", "another <em>good</em> tag"),
    ],
)
def test_content_has_allowed_html(data, expected):
    assert clean(data) == expected


def test_html_is_lowercased():
    assert (
        clean('<A HREF="http://example.com">foo</A>')
        == '<a href="http://example.com">foo</a>'
    )


@pytest.mark.parametrize(
    "data, should_strip, expected",
    [
        # Regular comment
        ("<!-- this is a comment -->", True, ""),
        # Open comment with no close comment bit
        ("<!-- open comment", True, ""),
        ("<!--open comment", True, ""),
        ("<!-- open comment", False, "<!-- open comment-->"),
        ("<!--open comment", False, "<!--open comment-->"),
        # Comment with text to the right
        ("<!-- comment -->text", True, "text"),
        ("<!--comment-->text", True, "text"),
        ("<!-- comment -->text", False, "<!-- comment -->text"),
        ("<!--comment-->text", False, "<!--comment-->text"),
        # Comment with text to the left
        ("text<!-- comment -->", True, "text"),
        ("text<!--comment-->", True, "text"),
        ("text<!-- comment -->", False, "text<!-- comment -->"),
        ("text<!--comment-->", False, "text<!--comment-->"),
    ],
)
def test_comments(data, should_strip, expected):
    assert clean(data, strip_comments=should_strip) == expected


def test_invalid_char_in_tag():
    assert (
        clean('<script/xss src="http://xx.com/xss.js"></script>')
        == '&lt;script/xss src="http://xx.com/xss.js"&gt;&lt;/script&gt;'
    )
    assert (
        clean('<script/src="http://xx.com/xss.js"></script>')
        == '&lt;script/src="http://xx.com/xss.js"&gt;&lt;/script&gt;'
    )


def test_unclosed_tag():
    assert clean("a <em>fixed tag") == "a <em>fixed tag</em>"
    assert (
        clean("<script src=http://xx.com/xss.js<b>")
        == "&lt;script src=http://xx.com/xss.js&lt;b&gt;"
    )
    assert (
        clean('<script src="http://xx.com/xss.js"<b>')
        == '&lt;script src="http://xx.com/xss.js"&lt;b&gt;'
    )
    assert (
        clean('<script src="http://xx.com/xss.js" <b>')
        == '&lt;script src="http://xx.com/xss.js" &lt;b&gt;'
    )


def test_nested_script_tag():
    assert (
        clean("<<script>script>evil()<</script>/script>")
        == "&lt;&lt;script&gt;script&gt;evil()&lt;&lt;/script&gt;/script&gt;"
    )
    assert (
        clean("<<x>script>evil()<</x>/script>")
        == "&lt;&lt;x&gt;script&gt;evil()&lt;&lt;/x&gt;/script&gt;"
    )
    assert (
        clean("<script<script>>evil()</script</script>>")
        == "&lt;script&lt;script&gt;&gt;evil()&lt;/script&lt;/script&gt;&gt;"
    )


@pytest.mark.parametrize(
    "text, expected",
    [
        ("an & entity", "an &amp; entity"),
        ("an < entity", "an &lt; entity"),
        ("tag < <em>and</em> entity", "tag &lt; <em>and</em> entity"),
    ],
)
def test_bare_entities_get_escaped_correctly(text, expected):
    assert clean(text) == expected


@pytest.mark.parametrize(
    "text, expected",
    [
        ("x<y", "x&lt;y"),
        ("<y", "&lt;y"),
        ("x < y", "x &lt; y"),
        ("<y>", "&lt;y&gt;"),
        # this is an eof-in-attribute-name parser error
        ("<some thing", "&lt;some thing"),
        # this is an eof-in-attribute-value-no-quotes parser error
        ("<some thing=foo", "&lt;some thing=foo"),
    ],
)
def test_lessthan_escaping(text, expected):
    # Tests whether < gets escaped correctly in a series of edge cases where
    # the html5lib tokenizer hits an error because it's not the beginning of a
    # tag.
    assert clean(text) == expected


@pytest.mark.parametrize(
    "text, expected",
    [
        # Test character entities in text don't get escaped
        ("&amp;", "&amp;"),
        ("&nbsp;", "&nbsp;"),
        ("&nbsp; test string &nbsp;", "&nbsp; test string &nbsp;"),
        ("&lt;em&gt;strong&lt;/em&gt;", "&lt;em&gt;strong&lt;/em&gt;"),
        # Test character entity at beginning of string doesn't get escaped
        ("&amp;is cool", "&amp;is cool"),
        # Test character entity at end of the string doesn't get escaped
        ("cool &amp;", "cool &amp;"),
        # Test bare ampersands before an entity at the beginning of the string
        # gets escaped
        ("&&amp; is cool", "&amp;&amp; is cool"),
        # Test ampersand after an entity at the end of the string gets escaped
        ("&amp; is cool &amp;&", "&amp; is cool &amp;&amp;"),
        # Test missing semi-colons mean we don't treat the thing as an entity--Bleach
        # only recognizes character entities that start with & and end with ;
        ("this &amp that", "this &amp;amp that"),
        (
            "http://example.com?active=true&current=true",
            "http://example.com?active=true&amp;current=true",
        ),
        # Test character entities in attribute values are not escaped
        ('<a href="?art&amp;copy">foo</a>', '<a href="?art&amp;copy">foo</a>'),
        ('<a href="?this=&gt;that">foo</a>', '<a href="?this=&gt;that">foo</a>'),
        # Things in attributes that aren't character entities get escaped
        (
            '<a href="http://example.com/&xx;">foo</a>',
            '<a href="http://example.com/&amp;xx;">foo</a>',
        ),
        (
            '<a href="http://example.com?&adp;">foo</a>',
            '<a href="http://example.com?&amp;adp;">foo</a>',
        ),
        (
            '<a href="http://example.com?active=true&current=true">foo</a>',
            '<a href="http://example.com?active=true&amp;current=true">foo</a>',
        ),
        # Things in text that aren't character entities get escaped
        ("&xx;", "&amp;xx;"),
        ("&adp;", "&amp;adp;"),
        ("&currdupe;", "&amp;currdupe;"),
        # Test numeric entities
        ("&#39;", "&#39;"),
        ("&#34;", "&#34;"),
        ("&#123;", "&#123;"),
        ("&#x0007b;", "&#x0007b;"),
        ("&#x0007B;", "&#x0007B;"),
        # Test non-numeric entities
        ("&#", "&amp;#"),
        ("&#<", "&amp;#&lt;"),
        # html5lib tokenizer unescapes character entities, so these would become '
        # and " which makes it possible to break out of html attributes.
        #
        # Verify that clean() doesn't unescape entities.
        ("&#39;&#34;", "&#39;&#34;"),
    ],
)
def test_character_entities_handling(text, expected):
    assert clean(text) == expected


@pytest.mark.parametrize(
    "data, kwargs, expected",
    [
        # All tags are allowed, so it strips nothing
        (
            "a test <em>with</em> <b>html</b> tags",
            {},
            "a test <em>with</em> <b>html</b> tags",
        ),
        # img tag is disallowed, so it's stripped
        (
            'a test <em>with</em> <img src="http://example.com/"> <b>html</b> tags',
            {},
            "a test <em>with</em>  <b>html</b> tags",
        ),
        # a tag is disallowed, so it's stripped
        (
            '<p><a href="http://example.com/">link text</a></p>',
            {"tags": {"p"}},
            "<p>link text</p>",
        ),
        # Test nested disallowed tag
        (
            "<p><span>multiply <span>nested <span>text</span></span></span></p>",
            {"tags": {"p"}},
            "<p>multiply nested text</p>",
        ),
        # (#271)
        ("<ul><li><script></li></ul>", {"tags": {"ul", "li"}}, "<ul><li></li></ul>"),
        # Test disallowed tag that's deep in the tree
        (
            '<p><a href="http://example.com/"><img src="http://example.com/"></a></p>',
            {"tags": {"a", "p"}},
            '<p><a href="http://example.com/"></a></p>',
        ),
        # Test isindex -- the parser expands this to a prompt (#279)
        ("<isindex>", {}, ""),
        # Test non-tags that are well-formed HTML (#280)
        ("Yeah right <sarcasm/>", {}, "Yeah right "),
        ("<sarcasm>", {}, ""),
        ("</sarcasm>", {}, ""),
        # These are non-tags, but also "malformed" so they don't get treated like
        # tags and stripped
        ("</ sarcasm>", {}, "&lt;/ sarcasm&gt;"),
        ("</ sarcasm >", {}, "&lt;/ sarcasm &gt;"),
        ("Foo <bar@example.com>", {}, "Foo "),
        ("Favorite movie: <name of movie>", {}, "Favorite movie: "),
        ("</3", {}, "&lt;/3"),
    ],
)
def test_stripping_tags(data, kwargs, expected):
    assert clean(data, strip=True, **kwargs) == expected
    assert clean(f"  {data}  ", strip=True, **kwargs) == f"  {expected}  "
    assert clean(f"abc {data} def", strip=True, **kwargs) == f"abc {expected} def"


@pytest.mark.parametrize(
    "data, expected",
    [
        # Disallowed tag is escaped
        (
            "<img src=\"javascript:alert('XSS');\">",
            "&lt;img src=\"javascript:alert('XSS');\"&gt;",
        ),
        # Test with parens
        ("<script>safe()</script>", "&lt;script&gt;safe()&lt;/script&gt;"),
        # Test with braces
        ("<style>body{}</style>", "&lt;style&gt;body{}&lt;/style&gt;"),
        # Test nested disallow tags (#271)
        ("<ul><li><script></li></ul>", "<ul><li>&lt;script&gt;</li></ul>"),
        # Test isindex -- the parser expands this to a prompt (#279)
        ("<isindex>", "&lt;isindex&gt;"),
        # Test non-tags (#280)
        ("<sarcasm/>", "&lt;sarcasm/&gt;"),
        ("<sarcasm>", "&lt;sarcasm&gt;"),
        ("</sarcasm>", "&lt;/sarcasm&gt;"),
        ("</ sarcasm>", "&lt;/ sarcasm&gt;"),
        ("</ sarcasm >", "&lt;/ sarcasm &gt;"),
        ("</3", "&lt;/3"),
        ("<bar@example.com>", "&lt;bar@example.com&gt;"),
        ("Favorite movie: <name of movie>", "Favorite movie: &lt;name of movie&gt;"),
    ],
)
def test_escaping_tags(data, expected):
    assert clean(data, strip=False) == expected
    assert clean(f"  {data}  ", strip=False) == f"  {expected}  "
    assert clean(f"abc {data} def", strip=False) == f"abc {expected} def"


@pytest.mark.parametrize(
    "data, expected",
    [
        ("<scri<script>pt>alert(1)</scr</script>ipt>", "pt&gt;alert(1)ipt&gt;"),
        ("<scri<scri<script>pt>pt>alert(1)</script>", "pt&gt;pt&gt;alert(1)"),
    ],
)
def test_stripping_tags_is_safe(data, expected):
    """Test stripping tags shouldn't result in malicious content"""
    assert clean(data, strip=True) == expected


def test_href_with_wrong_tag():
    assert clean('<em href="fail">no link</em>') == "<em>no link</em>"


def test_disallowed_attr():
    IMG = {"img"}
    IMG_ATTR = ["src"]

    assert clean('<a onclick="evil" href="test">test</a>') == '<a href="test">test</a>'
    assert (
        clean('<img onclick="evil" src="test" />', tags=IMG, attributes=IMG_ATTR)
        == '<img src="test">'
    )
    assert (
        clean('<img href="invalid" src="test" />', tags=IMG, attributes=IMG_ATTR)
        == '<img src="test">'
    )


def test_unquoted_attr_values_are_quoted():
    assert (
        clean("<abbr title=mytitle>myabbr</abbr>")
        == '<abbr title="mytitle">myabbr</abbr>'
    )


def test_unquoted_event_handler_attr_value():
    assert (
        clean('<a href="http://xx.com" onclick=foo()>xx.com</a>')
        == '<a href="http://xx.com">xx.com</a>'
    )


def test_invalid_filter_attr():
    IMG = {"img"}
    IMG_ATTR = {
        "img": lambda tag, name, val: name == "src" and val == "http://example.com/"
    }

    assert (
        clean(
            '<img onclick="evil" src="http://example.com/" />',
            tags=IMG,
            attributes=IMG_ATTR,
        )
        == '<img src="http://example.com/">'
    )
    assert (
        clean(
            '<img onclick="evil" src="http://badhost.com/" />',
            tags=IMG,
            attributes=IMG_ATTR,
        )
        == "<img>"
    )


def test_poster_attribute():
    """Poster attributes should not allow javascript."""
    tags = {"video"}
    attrs = {"video": ["poster"]}

    test = '<video poster="javascript:alert(1)"></video>'
    assert clean(test, tags=tags, attributes=attrs) == "<video></video>"

    ok = '<video poster="/foo.png"></video>'
    assert clean(ok, tags=tags, attributes=attrs) == ok


def test_attributes_callable():
    """Verify attributes can take a callable"""
    ATTRS = lambda tag, name, val: name == "title"
    TAGS = {"a"}

    text = '<a href="/foo" title="blah">example</a>'
    assert clean(text, tags=TAGS, attributes=ATTRS) == '<a title="blah">example</a>'


def test_attributes_wildcard():
    """Verify attributes[*] works"""
    ATTRS = {
        "*": ["id"],
        "img": ["src"],
    }
    TAGS = {"img", "em"}

    text = (
        'both <em id="foo" style="color: black">can</em> have <img id="bar" src="foo"/>'
    )
    assert (
        clean(text, tags=TAGS, attributes=ATTRS)
        == 'both <em id="foo">can</em> have <img id="bar" src="foo">'
    )


def test_attributes_wildcard_callable():
    """Verify attributes[*] callable works"""
    ATTRS = {"*": lambda tag, name, val: name == "title"}
    TAGS = {"a"}

    assert (
        clean('<a href="/foo" title="blah">example</a>', tags=TAGS, attributes=ATTRS)
        == '<a title="blah">example</a>'
    )


def test_attributes_tag_callable():
    """Verify attributes[tag] callable works"""

    def img_test(tag, name, val):
        return name == "src" and val.startswith("https")

    ATTRS = {
        "img": img_test,
    }
    TAGS = {"img"}

    text = 'foo <img src="http://example.com" alt="blah"> baz'
    assert clean(text, tags=TAGS, attributes=ATTRS) == "foo <img> baz"
    text = 'foo <img src="https://example.com" alt="blah"> baz'
    assert (
        clean(text, tags=TAGS, attributes=ATTRS)
        == 'foo <img src="https://example.com"> baz'
    )


def test_attributes_tag_list():
    """Verify attributes[tag] list works"""
    ATTRS = {"a": ["title"]}
    TAGS = {"a"}

    assert (
        clean('<a href="/foo" title="blah">example</a>', tags=TAGS, attributes=ATTRS)
        == '<a title="blah">example</a>'
    )


def test_attributes_list():
    """Verify attributes list works"""
    ATTRS = ["title"]
    TAGS = {"a"}

    text = '<a href="/foo" title="blah">example</a>'
    assert clean(text, tags=TAGS, attributes=ATTRS) == '<a title="blah">example</a>'


@pytest.mark.parametrize(
    "data, kwargs, expected",
    [
        # invalid URI (urlparse raises a ValueError: Invalid IPv6 URL)
        # is not allowed by default
        (
            '<a href="http://example.com]">text</a>',
            {"protocols": ALLOWED_PROTOCOLS},
            "<a>text</a>",
        ),
        # data protocol is not allowed by default
        (
            '<a href="data:text/javascript,prompt(1)">foo</a>',
            {"protocols": ALLOWED_PROTOCOLS},
            "<a>foo</a>",
        ),
        # javascript: is not allowed by default
        (
            "<a href=\"javascript:alert('XSS')\">xss</a>",
            {"protocols": ALLOWED_PROTOCOLS},
            "<a>xss</a>",
        ),
        # File protocol is not allowed by default
        (
            '<a href="file:///tmp/foo">foo</a>',
            {"protocols": ALLOWED_PROTOCOLS},
            "<a>foo</a>",
        ),
        # Specified protocols are allowed
        (
            '<a href="myprotocol://more_text">allowed href</a>',
            {"protocols": {"myprotocol"}},
            '<a href="myprotocol://more_text">allowed href</a>',
        ),
        # Unspecified protocols are not allowed
        (
            '<a href="http://example.com">invalid href</a>',
            {"protocols": {"myprotocol"}},
            "<a>invalid href</a>",
        ),
        # Anchors are ok
        (
            '<a href="#section-1">foo</a>',
            {"protocols": set()},
            '<a href="#section-1">foo</a>',
        ),
        # Anchor that looks like a domain is ok
        (
            '<a href="#example.com">foo</a>',
            {"protocols": set()},
            '<a href="#example.com">foo</a>',
        ),
        # Allow implicit http/https if allowed
        (
            '<a href="/path">valid</a>',
            {"protocols": {"http"}},
            '<a href="/path">valid</a>',
        ),
        (
            '<a href="/path">valid</a>',
            {"protocols": {"https"}},
            '<a href="/path">valid</a>',
        ),
        (
            '<a href="example.com">valid</a>',
            {"protocols": {"http"}},
            '<a href="example.com">valid</a>',
        ),
        (
            '<a href="example.com:8000">valid</a>',
            {"protocols": {"http"}},
            '<a href="example.com:8000">valid</a>',
        ),
        (
            '<a href="localhost">valid</a>',
            {"protocols": {"http"}},
            '<a href="localhost">valid</a>',
        ),
        (
            '<a href="localhost:8000">valid</a>',
            {"protocols": {"http"}},
            '<a href="localhost:8000">valid</a>',
        ),
        (
            '<a href="192.168.100.100">valid</a>',
            {"protocols": {"http"}},
            '<a href="192.168.100.100">valid</a>',
        ),
        (
            '<a href="192.168.100.100:8000">valid</a>',
            {"protocols": {"http"}},
            '<a href="192.168.100.100:8000">valid</a>',
        ),
        pytest.param(
            *(
                '<a href="192.168.100.100:8000/foo#bar">valid</a>',
                {"protocols": {"http"}},
                '<a href="192.168.100.100:8000/foo#bar">valid</a>',
            ),
            marks=pytest.mark.xfail,
        ),
        # Disallow