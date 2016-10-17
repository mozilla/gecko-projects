# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'includes': [
    '../../coreconf/config.gypi'
  ],
  'variables': {
    'module': 'nss'
  },
  'targets': [
    {
      'target_name': 'lib_cryptohi_exports',
      'type': 'none',
      'copies': [
        {
          'files': [
            'cryptohi.h',
            'cryptoht.h',
            'key.h',
            'keyhi.h',
            'keyt.h',
            'keythi.h',
            'sechash.h'
          ],
          'destination': '<(PRODUCT_DIR)/dist/<(module)/public'
        },
        {
          'files': [
            'keyi.h',
          ],
          'destination': '<(PRODUCT_DIR)/dist/<(module)/private'
        }
      ]
    }
  ],
}
