# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
{
  'includes': [
    '../../coreconf/config.gypi'
  ],
  'targets': [
    {
      'target_name': 'lib_jar_exports',
      'type': 'none',
      'copies': [
        {
          'files': [
            'jar-ds.h',
            'jar.h',
            'jarfile.h'
          ],
          'destination': '<(nss_dist_dir)/public/<(module)'
        }
      ]
    }
  ],
  'variables': {
    'module': 'nss'
  }
}
