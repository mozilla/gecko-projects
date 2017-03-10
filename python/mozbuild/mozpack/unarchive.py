# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import

import bz2
import gzip
import stat
import tarfile

def unpack_archive(filename, outputdir):
    if not tarfile.is_tarfile(filename):
        raise Exception("File %s is not a valid tarfile." % filename)

    tar = tarfile.open(filename)
    tar.extractall(path=outputdir)
    tar.close()
