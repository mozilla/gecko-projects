# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest

from compare_locales import parser


class TestParserContext(unittest.TestCase):
    def test_lines(self):
        "Test that Parser.Context.lines returns 1-based tuples"
        ctx = parser.Parser.Context('''first line
second line
third line
''')
        self.assertEqual(
            ctx.lines(0, 1),
            [(1, 1), (1, 2)]
        )
        self.assertEqual(
            ctx.lines(len('first line')),
            [(1, len('first line') + 1)]
        )
        self.assertEqual(
            ctx.lines(len('first line') + 1),
            [(2, 1)]
        )
        self.assertEqual(
            ctx.lines(len(ctx.contents)),
            [(4, 1)]
        )

    def test_empty_parser(self):
        p = parser.Parser()
        entities, _map = p.parse()
        self.assertListEqual(
            entities,
            []
        )
        self.assertDictEqual(
            _map,
            {}
        )
