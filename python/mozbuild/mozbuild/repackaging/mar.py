# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import tempfile
import shutil
import zipfile
import subprocess
import mozpack.path as mozpath
from application_ini import get_application_ini_value

def repackage_mar(topsrcdir, package, mar, output):
    if not zipfile.is_zipfile(package):
        raise Exception("Package file %s is not a valid .zip file." % package)

    tmpdir = tempfile.mkdtemp()
    try:
        z = zipfile.ZipFile(package)
        z.extractall(tmpdir)
        filelist = z.namelist()
        z.close()

        # Make sure the .zip file just contains a directory like 'firefox/' at
        # the top, and find out what it is called.
        toplevel_dirs = set([mozpath.split(f)[0] for f in filelist])
        if len(toplevel_dirs) != 1:
            raise Exception("Package file is expected to have a single top-level directory (eg: 'firefox'), not: %s" % toplevel_dirs)
        ffxdir = mozpath.join(tmpdir, toplevel_dirs.pop())

        make_full_update = mozpath.join(topsrcdir, 'tools/update-packaging/make_full_update.sh')

        env = os.environ.copy()
        env['MOZ_FULL_PRODUCT_VERSION'] = get_application_ini_value(tmpdir, 'App', 'Version')
        env['MAR'] = mar

        subprocess.check_call([env['SHELL'], make_full_update, output, ffxdir], env=env)

    finally:
        shutil.rmtree(tmpdir)
