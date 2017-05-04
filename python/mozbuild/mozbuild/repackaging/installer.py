# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import tempfile
import shutil
import zipfile
import mozpack.path as mozpath
from mozbuild.action.exe_7z_archive import archive_exe

def repackage_installer(topsrcdir, tag, setupexe, package, output):
    if package and not zipfile.is_zipfile(package):
        raise Exception("Package file %s is not a valid .zip file." % package)

    # TODO: Get the full path for the tag since we chdir later, so a relative
    # path may not point to where we think.
    tag = mozpath.realpath(tag)
    output = mozpath.realpath(output)

    tmpdir = tempfile.mkdtemp()
    saved_dir = os.getcwd()
    try:
        if package:
            z = zipfile.ZipFile(package)
            z.extractall(tmpdir)
            z.close()

        # Copy setup.exe into the root of the install dir, alongside the
        # package.
        shutil.copyfile(setupexe, mozpath.join(tmpdir, mozpath.basename(setupexe)))

        # TODO: Make archive_exe work better here? Maybe take setup.exe as an
        # input instead of assuming we're in the dir with 'firefox' and
        # setup.exe already?
        os.chdir(tmpdir)

        sfx_package = mozpath.join(topsrcdir, 'other-licenses/7zstub/firefox/7zSD.sfx')

        package_name = 'firefox' if package else None
        archive_exe(package_name, tag, sfx_package, output)

    finally:
        # TODO: 
        # 2) cleanup tmpdir in archive_exe
        # 3) fix chdiring
        # 4) fix where to find setup.exe
        os.chdir(saved_dir)
        shutil.rmtree(tmpdir)
