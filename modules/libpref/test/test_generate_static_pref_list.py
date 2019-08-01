# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function

import mozpack.path as mozpath
import mozunit
import sys
import unittest
import yaml
from os import path
from StringIO import StringIO

sys.path.append(path.join(path.dirname(__file__), ".."))
from init.generate_static_pref_list import generate_headers

test_data_path = mozpath.abspath(mozpath.dirname(__file__))
test_data_path = mozpath.join(test_data_path, 'data')

# A single good input with lots of different combinations. All the prefs start
# with "my." so they end up in the same group, which simplifies the comparison
# against the expected output.
good_input = '''
- name: my.bool
  type: bool
  value: false
  mirror: never

- name: my.int
  type: int32_t
  value: -123
  mirror: once
  do_not_use_directly: false

- mirror: always
  value: 999
  type: uint32_t
  name: my.uint

- name: my.float            # A comment.
  type: float               # A comment.
  do_not_use_directly: true # A comment.
  value: 0.0f               # A comment.
  mirror: once              # A comment.

# A comment.
- name: my.string
  type: String
  value: foo"bar    # The double quote needs escaping.
  mirror: never
  include: foobar.h

# A comment.
- name: my.string2
  type: String
  value: "foobar"    # This string is quoted.
  mirror: never

# A comment.
- name: my.atomic.bool
  type: RelaxedAtomicBool
  value: true
  mirror: always

# YAML+Python interprets `10 + 10 * 20` as a string, and so it is printed
# unchanged.
- name: my.atomic.int
  type: ReleaseAcquireAtomicInt32
  value: 10 + 10 * 20
  mirror: always
  do_not_use_directly: true # A comment.

# YAML+Python changes `0x44` to `68` because it interprets the value as an
# integer.
- name: my.atomic.uint
  type: SequentiallyConsistentAtomicUint32
  value: 0x44
  mirror: once

# YAML+Python changes `.4455667` to `0.4455667` because it interprets the value
# as a float.
- name: my.atomic.float
  type: AtomicFloat
  value: .4455667
  mirror: never
  include: <math.h>
'''

# The corresponding output for good_input.
good_output = '''\
PREF("my.bool", bool, false)

VARCACHE_PREF(
  Once,
  "my.int",
   my_int,
   my_int_AtStartup,
  int32_t, -123
)

VARCACHE_PREF(
  Live,
  "my.uint",
   my_uint,
   my_uint,
  uint32_t, 999
)

VARCACHE_PREF(
  Once,
  "my.float",
   my_float,
   my_float_AtStartup_DoNotUseDirectly,
  float, 0.0f
)

PREF("my.string", String, "foo\\"bar")

PREF("my.string2", String, "foobar")

VARCACHE_PREF(
  Live,
  "my.atomic.bool",
   my_atomic_bool,
   my_atomic_bool,
  RelaxedAtomicBool, true
)

VARCACHE_PREF(
  Live,
  "my.atomic.int",
   my_atomic_int,
   my_atomic_int_DoNotUseDirectly,
  ReleaseAcquireAtomicInt32, 10 + 10 * 20
)

VARCACHE_PREF(
  Once,
  "my.atomic.uint",
   my_atomic_uint,
   my_atomic_uint_AtStartup,
  SequentiallyConsistentAtomicUint32, 68
)

PREF("my.atomic.float", AtomicFloat, 0.4455667)
'''

# A lot of bad inputs, each with an accompanying error message. Listed in order
# of the relevant `error` calls within generate_static_pref_list.py.
bad_inputs = [
    ('''
- name: do_not_use_directly.uselessly.set
  type: int32_t
  value: 0
  mirror: never
  do_not_use_directly: true
''', '`do_not_use_directly` uselessly set with `mirror` value `never` for '
        'pref `do_not_use_directly.uselessly.set`'),

    ('''
- invalidkey: 3
''', 'invalid key `invalidkey`'),

    ('''
- type: int32_t
''', 'missing `name` key'),

    ('''
- name: 99
''', 'non-string `name` value `99`'),

    ('''
- name: name_with_no_dot
''', '`name` value `name_with_no_dot` lacks a \'.\''),

    ('''
- name: pref.is.defined.more.than.once
  type: bool
  value: false
  mirror: never
- name: pref.is.defined.more.than.once
  type: int32_t
  value: 111
  mirror: always
''', '`pref.is.defined.more.than.once` pref is defined more than once'),

    ('''
- name: your.pref
  type: bool
  value: false
  mirror: never
- name: my.pref
  type: bool
  value: false
  mirror: never
''', '`my.pref` pref must come before `your.pref` pref'),

    ('''
- name: missing.type.key
  value: false
  mirror: never
''', 'missing `type` key for pref `missing.type.key`'),

    ('''
- name: invalid.type.value
  type: const char*
  value: true
  mirror: never
''', 'invalid `type` value `const char*` for pref `invalid.type.value`'),

    ('''
- name: missing.value.key
  type: int32_t
  mirror: once
''', 'missing `value` key for pref `missing.value.key`'),

    ('''
- name: non-string.value
  type: String
  value: 3.45
  mirror: once
''', 'non-string `value` value `3.45` for `String` pref `non-string.value`; add double quotes'),

    ('''
- name: invalid.boolean.value
  type: bool
  value: true || false
  mirror: once
''', 'invalid boolean value `true || false` for pref `invalid.boolean.value`'),

    ('''
- name: missing.mirror.key
  type: int32_t
  value: 3
''', 'missing `mirror` key for pref `missing.mirror.key`'),

    ('''
- name: invalid.mirror.value
  type: bool
  value: true
  mirror: sometimes
''', 'invalid `mirror` value `sometimes` for pref `invalid.mirror.value`'),

    ('''
- name: non-boolean.do_not_use_directly.value
  type: bool
  value: true
  mirror: always
  do_not_use_directly: 0
''', 'non-boolean `do_not_use_directly` value `0` for pref '
     '`non-boolean.do_not_use_directly.value`'),

    ('''
- name: non-string.include.value
  type: bool
  value: true
  mirror: always
  include: 33
''', 'non-string `include` value `33` for pref `non-string.include.value`'),

    ('''
- name: include.value.starts.with
  type: float
  value: M_PI
  mirror: never
  include: <cmath
''', '`include` value `<cmath` starts with `<` but does not end with `>` for '
     'pref `include.value.starts.with`'),
]


class TestGenerateStaticPrefList(unittest.TestCase):
    '''
    Unit tests for generate_static_pref_list.py.
    '''

    def test_good(self):
        'Test various pieces of good input.'
        inp = StringIO(good_input)
        pref_list = yaml.safe_load(inp)
        (code, includes) = generate_headers(pref_list)
        self.assertEqual(good_output, '\n'.join(code['my']))

    def test_bad(self):
        'Test various pieces of bad input.'

        for (input_string, expected) in bad_inputs:
            inp = StringIO(input_string)
            try:
                pref_list = yaml.safe_load(inp)
                generate_headers(pref_list)
                self.assertEqual(0, 1)
            except ValueError as e:
                self.assertEqual(str(e), expected)


if __name__ == '__main__':
    mozunit.main()
