import re

from urllib.parse import quote

from bleach import callbacks as linkify_callbacks
from bleach import html5lib_shim


#: List of default callbacks
DEFAULT_CALLBACKS = [linkify_callbacks.nofollow]


TLDS = """ac ad ae aero af ag ai al am an ao aq ar arpa as asia at au aw ax az
       ba bb bd be bf bg bh bi biz bj bm bn bo br bs bt bv bw by bz ca cat
       cc cd cf cg ch ci ck cl cm cn co com coop cr cu cv cx cy cz de dj dk
       dm do dz ec edu ee eg er es et eu fi fj fk fm fo fr ga gb gd ge gf gg
       gh gi gl gm gn gov gp gq gr gs gt gu gw gy hk hm hn hr ht hu id ie il
       im in info int io iq ir is it je jm jo jobs jp ke kg kh ki km kn kp
       kr kw ky kz la lb lc li lk lr ls lt lu lv ly ma mc md me mg mh mil mk
       ml mm mn mo mobi mp mq mr ms mt mu museum mv mw mx my mz na name nc ne
       net nf ng ni nl no np nr nu nz om org pa pe pf pg ph pk pl pm pn post
       pr pro ps pt pw py qa re ro rs ru rw sa sb sc sd se sg sh si sj sk sl
       sm sn so sr ss st su sv sx sy sz tc td tel tf tg th tj tk tl tm tn to
       tp tr travel tt tv tw tz ua ug uk us uy uz va vc ve vg vi vn vu wf ws
       xn xxx ye yt yu za zm zw""".split()

# Make sure that .com doesn't get matched by .co first
TLDS.reverse()


def build_url_re(tlds=TLDS, protocols=html5lib_shim.allowed_protocols):
    """Builds the url regex used by linkifier

    If you want a different set of tlds or allowed protocols, pass those in
    and stomp on the existing ``url_re``::

        from bleach import linkifier

        my_url_re = linkifier.build_url_re(my_tlds_list, my_protocols)

        linker = LinkifyFilter(url_re=my_url_re)

    """
    return re.compile(
        r"""\(*  # Match any opening parentheses.
        \b(?<![@.])(?:(?:{0}):/{{0,3}}(?:(?:\w+:)?\w+@)?)?  # http://
        ([\w-]+\.)+(?:{1})(?:\:[0-9]+)?(?!\.\w)\b   # xx.yy.tld(:##)?
        (?:[/?][^\s\{{\}}\|\\\^`<>"]*)?
            # /path/zz (excluding "unsafe" chars from RFC 3986,
            # except for # and ~, which happen in practice)
        """.format(
            "|".join(sorted(protocols)), "|".join(sorted(tlds))
        ),
        re.IGNORECASE | re.VERBOSE | re.UNICODE,
    )


URL_RE = build_url_re()


PROTO_RE = re.compile(r"^[\w-]+:/{0,3}", re.IGNORECASE)


def build_email_re(tlds=TLDS):
    """Builds the email regex used by linkifier

    If you want a different set of tlds, pass those in and stomp on the existing ``email_re``::

        from bleach import linkifier

        my_email_re = linkifier.build_email_re(my_tlds_list)

        linker = LinkifyFilter(email_re=my_url_re)

    """
    # open and closing braces doubled below for format string
    return re.compile(
        r"""(?<!//)
        (([-!#$%&'*+/=?^_`{{}}|~0-9A-Z]+
            (\.[-!#$%&'*+/=?^_`{{}}|~0-9A-Z]+)*  # dot-atom
        |^"([\001-\010\013\014\016-\037!#-\[\]-\177]
            |\\[\001-\011\013\014\016-\177])*"  # quoted-string
        )@(?:[A-Z0-9](?:[A-Z0-9-]{{0,61}}[A-Z0-9])?\.)+(?:{0}))  # domain
        """.format(
            "|".join(tlds)
        ),
        re.IGNORECASE | re.MULTILINE | re.VERBOSE,
    )


EMAIL_RE = build_email_re()


class Linker:
    """Convert URL-like strings in an HTML fragment to links

    This function converts strings that look like URLs, domain names and email
    addresses in text that may be an HTML fragment to links, while preserving:

    1. links already in the string
    2. urls found in attributes
    3. email addresses

    linkify does a best-effort approach and tries to recover from bad
    situations due to crazy text.

    """

    def __init__(
        self,
        callbacks=DEFAULT_CALLBACKS,
        skip_tags=None,
        parse_email=False,
        url_re=URL_RE,
        email_re=EMAIL_RE,
        recognized_tags=html5lib_shim.HTML_TAGS,
    ):
        """Creates a Linker instance

        :arg list callbacks: list of callbacks to run when adjusting tag attributes;
            defaults to ``bleach.linkifier.DEFAULT_CALLBACKS``

        :arg set skip_tags: set of tags that you don't want to linkify the
            contents of; for example, you could set this to ``{'pre'}`` to skip
            linkifying contents of ``pre`` tags; ``None`` means you don't
            want linkify to skip any tags

        :arg bool parse_email: whether or not to linkify email addresses

        :arg url_re: url matching regex

        :arg email_re: email matching regex

        :arg set recognized_tags: the set of tags that linkify knows about;
            everything else gets escaped

        :returns: linkified text as unicode

        """
        self.callbacks = callbacks
        self.skip_tags = skip_tags
        self.parse_email = parse_email
        self.url_re = url_re
        self.email_re = email_re

        # Create a parser/tokenizer that allows all HTML tags and escapes
        # anything not in that list.
        self.parser = html5lib_shim.BleachHTMLParser(
            tags=frozenset(recognized_tags),
            strip=False,
            consume_entities=False,
            namespaceHTMLElements=False,
        )
        self.walker = html5lib_shim.getTreeWalker("etree")
        self.serializer = html5lib_shim.BleachHTMLSerializer(
            quote_attr_values="always",
            omit_optional_tags=False,
            # We want to leave entities as they are without escaping or
            # resolving or expanding
            resolve_entities=False,
            # linkify does not sanitize
            sanitize=False,
            # linkify preserves attr order
            alphabetical_attributes=False,
        )

    def linkify(self, text):
        """Linkify specified text

        :arg str text: the text to add links to

        :returns: linkified text as unicode

        :raises TypeError: if ``text`` is not a text type

        """
        if not isinstance(text, str):
            raise TypeError("argument must be of text type")

        if not text:
            return ""

        dom = self.parser.parseFragment(text)
        filtered = LinkifyFilter(
            source=self.walker(dom),
            callbacks=self.callbacks,
            skip_tags=self.skip_tags,
            parse_email=self.parse_email,
            url_re=self.url_re,
            email_re=self.email_re,
        )
        return self.serializer.render(filtered)


class LinkifyFilter(html5lib_shim.Filter):
    """html5lib filter that linkifies text

    This will do the following:

    * convert email addresses into links
    * convert urls into links
    * edit existing links by running them through callbacks--the default is to
      add a ``rel="nofollow"``

    This filter can be used anywhere html5lib filters can be used.

    """

    def __init__(
        self,
        source,
        callbacks=DEFAULT_CALLBACKS,
        skip_tags=None,
        parse_email=False,
        url_re=URL_RE,
        email_re=EMAIL_RE,
    ):
        """Creates a LinkifyFilter instance

        :arg source: stream as an html5lib TreeWalker

        :arg list callbacks: list of callbacks to run when adjusting tag attributes;
            defaults to ``bleach.linkifier.DEFAULT_CALLBACKS``

        :arg set skip_tags: set of tags that you don't want to linkify the
            contents of; for example, you could set this to ``{'pre'}`` to skip
            linkifying contents of ``pre`` tags

        :