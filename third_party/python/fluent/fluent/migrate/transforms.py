# coding=utf8
"""Migration Transforms.

Transforms are AST nodes which describe how legacy translations should be
migrated.  They are created inert and only return the migrated AST nodes when
they are evaluated by a MergeContext.

All Transforms evaluate to Fluent Patterns. This makes them suitable for
defining migrations of values of message, attributes and variants.  The special
CONCAT Transform is capable of joining multiple Patterns returned by evaluating
other Transforms into a single Pattern.  It can also concatenate Pattern
elements: TextElements and Placeables.

The COPY, REPLACE and PLURALS Transforms inherit from Source which is a special
AST Node defining the location (the file path and the id) of the legacy
translation.  During the migration, the current MergeContext scans the
migration spec for Source nodes and extracts the information about all legacy
translations being migrated. For instance,

    COPY('file.dtd', 'hello')

is equivalent to:

    FTL.Pattern([
        Source('file.dtd', 'hello')
    ])

Sometimes it's useful to work with text rather than (path, key) source
definitions. This is the case when the migrated translation requires some
hardcoded text, e.g. <a> and </a> when multiple translations become a single
one with a DOM overlay. In such cases it's best to use FTL.TextElements:

    FTL.Message(
        id=FTL.Identifier('update-failed'),
        value=CONCAT(
            COPY('aboutDialog.dtd', 'update.failed.start'),
            FTL.TextElement('<a>'),
            COPY('aboutDialog.dtd', 'update.failed.linkText'),
            FTL.TextElement('</a>'),
            COPY('aboutDialog.dtd', 'update.failed.end'),
        )
    )

The REPLACE_IN_TEXT Transform also takes TextElements as input, making it
possible to pass it as the foreach function of the PLURALS Transform. In the
example below, each slice of the plural string is converted into a
TextElement by PLURALS and then run through the REPLACE_IN_TEXT transform.

    FTL.Message(
        FTL.Identifier('delete-all'),
        value=PLURALS(
            'aboutDownloads.dtd',
            'deleteAll',
            VARIABLE_REFERENCE('num'),
            lambda text: REPLACE_IN_TEXT(
                text,
                {
                    '#1': VARIABLE_REFERENCE('num')
                }
            )
        )
    )
"""

from __future__ import unicode_literals
from __future__ import absolute_import
import re

import fluent.syntax.ast as FTL
from .errors import NotSupportedError


def evaluate(ctx, node):
    def eval_node(subnode):
        if isinstance(subnode, Transform):
            return subnode(ctx)
        else:
            return subnode

    return node.traverse(eval_node)


def get_text(element):
    '''Get text content of a PatternElement.'''
    if isinstance(element, FTL.TextElement):
        return element.value
    if isinstance(element, FTL.Placeable):
        if isinstance(element.expression, FTL.StringLiteral):
            return element.expression.value
        else:
            return None
    raise RuntimeError('Expected PatternElement')


def chain_elements(elements):
    '''Flatten a list of FTL nodes into an iterator over PatternElements.'''
    for element in elements:
        if isinstance(element, FTL.Pattern):
            # PY3 yield from element.elements
            for child in element.elements:
                yield child
        elif isinstance(element, FTL.PatternElement):
            yield element
        elif isinstance(element, FTL.Expression):
            yield FTL.Placeable(element)
        else:
            raise RuntimeError(
                'Expected Pattern, PatternElement or Expression')


re_leading_ws = re.compile(
    r'\A(?:(?P<whitespace> +)(?P<text>.*?)|(?P<block_text>\n.*?))\Z',
    re.S,
)
re_trailing_ws = re.compile(
    r'\A(?:(?P<text>.*?)(?P<whitespace> +)|(?P<block_text>.*\n))\Z',
    re.S
)


def extract_whitespace(regex, element):
    '''Extract leading or trailing whitespace from a TextElement.

    Return a tuple of (Placeable, TextElement) in which the Placeable
    encodes the extracted whitespace as a StringLiteral and the
    TextElement has the same amount of whitespace removed. The
    Placeable with the extracted whitespace is always returned first.
    If the element starts or ends with a newline, add an empty
    StringLiteral.
    '''
    match = re.search(regex, element.value)
    if match:
        # If white-space is None, we're a newline. Add an
        # empty { "" }
        whitespace = match.group('whitespace') or ''
        placeable = FTL.Placeable(FTL.StringLiteral(whitespace))
        if whitespace == element.value:
            return placeable, None
        else:
            # Either text or block_text matched the rest.
            text = match.group('text') or match.group('block_text')
            return placeable, FTL.TextElement(text)
    else:
        return None, element


class Transform(FTL.BaseNode):
    def __call__(self, ctx):
        raise NotImplementedError

    @staticmethod
    def pattern_of(*elements):
        normalized = []

        # Normalize text content: convert all text to TextElements, join
        # adjacent text and prune empty.
        for current in chain_elements(elements):
            current_text = get_text(current)
            if current_text is None:
                normalized.append(current)
                continue

            previous = normalized[-1] if len(normalized) else None
            if isinstance(previous, FTL.TextElement):
                # Join adjacent TextElements
                previous.value += current_text
            elif len(current_text) > 0:
                # Normalize non-empty text to a TextElement
                normalized.append(FTL.TextElement(current_text))
            else:
                # Prune empty text
                pass

        # Handle empty values
        if len(normalized) == 0:
            empty = FTL.Placeable(FTL.StringLiteral(''))
            return FTL.Pattern([empty])

        # Handle explicit leading whitespace
        if isinstance(normalized[0], FTL.TextElement):
            ws, text = extract_whitespace(re_leading_ws, normalized[0])
            normalized[:1] = [ws, text]

        # Handle explicit trailing whitespace
        if isinstance(normalized[-1], FTL.TextElement):
            ws, text = extract_whitespace(re_trailing_ws, normalized[-1])
            normalized[-1:] = [text, ws]

        return FTL.Pattern([
            element
            for element in normalized
            if element is not None
        ])


class Source(Transform):
    """Declare the source translation to be migrated with other transforms.

    When evaluated, `Source` returns a TextElement with the content from the
    source translation. Escaped characters are unescaped by the
    compare-locales parser according to the file format:

      - in properties files: \\uXXXX,
      - in DTD files: known named, decimal, and hexadecimal HTML entities.

    Consult the following files for the list of known named HTML entities:

    https://github.com/python/cpython/blob/2.7/Lib/htmlentitydefs.py
    https://github.com/python/cpython/blob/3.6/Lib/html/entities.py

    """

    def __init__(self, path, key, trim=False):
        if path.endswith('.ftl'):
            raise NotSupportedError(
                'Migrating translations from Fluent files is not supported '
                '({})'.format(path))

        self.path = path
        self.key = key
        self.trim = trim

    def get_text(self, ctx):
        return ctx.get_source(self.path, self.key)

    @staticmethod
    def trim_text(text):
        # strip leading white-space
        text = re.sub('^[ \t]+', '', text, flags=re.M)
        # strip trailing white-space
        text = re.sub('[ \t]+$', '', text, flags=re.M)
        # strip leading and trailing empty lines
        text = text.strip('\r\n')
        return text

    def __call__(self, ctx):
        text = self.get_text(ctx)
        if self.trim:
            text = self.trim_text(text)
        return FTL.TextElement(text)


class COPY(Source):
    """Create a Pattern with the translation value from the given source."""

    def __call__(self, ctx):
        element = super(COPY, self).__call__(ctx)
        return Transform.pattern_of(element)


PRINTF = re.compile(
    r'%(?P<good>%|'
    r'(?:(?P<number>[1-9][0-9]*)\$)?'
    r'(?P<width>\*|[0-9]+)?'
    r'(?P<prec>\.(?:\*|[0-9]+)?)?'
    r'(?P<spec>[duxXosScpfg]))'
)


def number():
    i = 1
    while True:
        yield i
        i += 1


def normalize_printf(text):
    """Normalize printf arguments so that they're all numbered.
    Gecko forbids mixing unnumbered and numbered ones, so
    we just need to convert unnumbered to numbered ones.
    Also remove ones that have zero width, as they're intended
    to be removed from the output by the localizer.
    """
    next_number = number()

    def normalized(match):
        if match.group('good') == '%':
            return '%'
        hidden = match.group('width') == '0'
        if match.group('number'):
            return '' if hidden else match.group()
        num = next(next_number)
        return '' if hidden else '%{}${}'.format(num, match.group('spec'))

    return PRINTF.sub(normalized, text)


class REPLACE_IN_TEXT(Transform):
    """Create a Pattern from a TextElement and replace legacy placeables.

    The original placeables are defined as keys on the `replacements` dict.
    For each key the value must be defined as a FTL Pattern, Placeable,
    TextElement or Expression to be interpolated.
    """

    def __init__(self, element, replacements, normalize_printf=False):
        self.element = element
        self.replacements = replacements
        self.normalize_printf = normalize_printf

    def __call__(self, ctx):
        # For each specified replacement, find all indices of the original
        # placeable in the source translation. If missing, the list of indices
        # will be empty.
        value = self.element.value
        if self.normalize_printf:
            value = normalize_printf(value)
        key_indices = {
            key: [m.start() for m in re.finditer(re.escape(key), value)]
            for key in self.replacements.keys()
        }

        # Build a dict of indices to replacement keys.
        keys_indexed = {}
        for key, indices in key_indices.items():
            for index in indices:
                keys_indexed[index] = key

        # Order the replacements by the position of the original placeable in
        # the translation.
        replacements = (
            (key, evaluate(ctx, self.replacements[key]))
            for index, key
            in sorted(keys_indexed.items(), key=lambda x: x[0])
        )

        # A list of PatternElements built from the legacy translation and the
        # FTL replacements. It may contain empty or adjacent TextElements.
        elements = []
        tail = value

        # Convert original placeables and text into FTL Nodes. For each
        # original placeable the translation will be partitioned around it and
        # the text before it will be converted into an `FTL.TextElement` and
        # the placeable will be replaced with its replacement.
        for key, node in replacements:
            before, key, tail = tail.partition(key)
            elements.append(FTL.TextElement(before))
            elements.append(node)

        # Don't forget about the tail after the loop ends.
        elements.append(FTL.TextElement(tail))
        return Transform.pattern_of(*elements)


class REPLACE(Source):
    """Create a Pattern with interpolations from given source.

    Interpolations in the translation value from the given source will be
    replaced with FTL placeables using the `REPLACE_IN_TEXT` transform.
    """

    def __init__(
        self, path, key, replacements,
        normalize_printf=False, **kwargs
    ):
        super(REPLACE, self).__init__(path, key, **kwargs)
        self.replacements = replacements
        self.normalize_printf = normalize_printf

    def __call__(self, ctx):
        element = super(REPLACE, self).__call__(ctx)
        return REPLACE_IN_TEXT(
            element, self.replacements,
            normalize_printf=self.normalize_printf
        )(ctx)


class PLURALS(Source):
    """Create a Pattern with plurals from given source.

    Build an `FTL.SelectExpression` with the supplied `selector` and variants
    extracted from the source. The original translation should be a
    semicolon-separated list of plural forms. Each form will be converted
    into a TextElement and run through the `foreach` function, which should
    return an `FTL.Node` or a `Transform`. By default, the `foreach` function
    creates a valid Pattern from the TextElement passed into it.
    """
    DEFAULT_ORDER = ('zero', 'one', 'two', 'few', 'many', 'other')

    def __init__(self, path, key, selector, foreach=Transform.pattern_of,
                 **kwargs):
        super(PLURALS, self).__init__(path, key, **kwargs)
        self.selector = selector
        self.foreach = foreach

    def __call__(self, ctx):
        element = super(PLURALS, self).__call__(ctx)
        selector = evaluate(ctx, self.selector)
        keys = ctx.plural_categories
        forms = [
            FTL.TextElement(part)
            for part in element.value.split(';')
        ]

        # The default CLDR form should be the last we have in DEFAULT_ORDER,
        # usually `other`, but in some cases `many`. If we don't have a variant
        # for that, we'll append one, using the, in CLDR order, last existing
        # variant in the legacy translation. That may or may not be the last
        # variant.
        default_key = [
            key for key in reversed(self.DEFAULT_ORDER) if key in keys
        ][0]

        # Match keys to legacy forms in the order they are defined in Gecko's
        # PluralForm.jsm. Filter out empty forms.
        pairs = [
            (key, var)
            for key, var in zip(keys, forms)
            if var.value
        ]

        # A special case for legacy translations which don't define any
        # plural forms.
        if len(pairs) == 0:
            return Transform.pattern_of()

        # A special case for languages with one plural category or one legacy
        # variant. We don't need to insert a SelectExpression for them.
        if len(pairs) == 1:
            _, only_form = pairs[0]
            only_variant = evaluate(ctx, self.foreach(only_form))
            return Transform.pattern_of(only_variant)

        # Make sure the default key is defined. If it's missing, use the last
        # form (in CLDR order) found in the legacy translation.
        pairs.sort(key=lambda pair: self.DEFAULT_ORDER.index(pair[0]))
        last_key, last_form = pairs[-1]
        if last_key != default_key:
            pairs.append((default_key, last_form))

        def createVariant(key, form):
            # Run the legacy plural form through `foreach` which returns an
            # `FTL.Node` describing the transformation required for each
            # variant. Then evaluate it to a migrated FTL node.
            value = evaluate(ctx, self.foreach(form))
            return FTL.Variant(
                key=FTL.Identifier(key),
                value=value,
                default=key == default_key
            )

        select = FTL.SelectExpression(
            selector=selector,
            variants=[
                createVariant(key, form)
                for key, form in pairs
            ]
        )

        return Transform.pattern_of(select)


class CONCAT(Transform):
    """Create a new Pattern from Patterns, PatternElements and Expressions."""

    def __init__(self, *elements, **kwargs):
        # We want to support both passing elements as *elements in the
        # migration specs and as elements=[]. The latter is used by
        # FTL.BaseNode.traverse when it recreates the traversed node using its
        # attributes as kwargs.
        self.elements = list(kwargs.get('elements', elements))

    def __call__(self, ctx):
        return Transform.pattern_of(*self.elements)
