import os
import platform
import sys

download_config = {
        "target.zip": os.environ.get("SIGNED_ZIP"),
        "setup.exe": os.environ.get("SIGNED_SETUP"),
        "mar.exe": os.environ.get("UNSIGNED_MAR"),
    }
repackage_config = [[
        "installer", "--package", "target.zip", "--tag",
        "browser/installer/windows/app.tag", "--setupexe", "setup.exe", "-o",
        "target.installer.exe"
    ], [
        "mar", "-i", "target.zip", "--mar", "mar.exe", "-o",
        "target.complete.mar"
    ]]

if '64' in platform.machine():
    plat = "win64"
else:
    plat = "win32"
    # Extend entries for stub installer, only built on win32
    download_config["setup-stub.exe"] = os.environ.get("SIGNED_SETUP_STUB")
    repackage_config.append([
        "installer", "--tag", "browser/installer/windows/stub.tag",
         "--setupexe", "setup-stub.exe", "-o", "target.stub-installer.exe"
    ])

config = {
    "input_home": "/home/worker/workspace/inputs",
    "src_mozconfig": "browser/config/mozconfigs/{}/repack".format(plat),

    "download_config": download_config,

    "repackage_config": repackage_config,

    # ToolTool
    "tooltool_manifest_src": 'browser/config/tooltool-manifests/{}/releng.manifest'.format(plat),
    "tooltool_url": 'http://relengapi/tooltool/',
    'tooltool_script': [sys.executable,
                        'C:/mozilla-build/tooltool.py'],
    'tooltool_cache': os.environ.get('TOOLTOOL_CACHE'),
}
