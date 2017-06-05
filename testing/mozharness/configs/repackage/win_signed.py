import os
import platform
import sys

download_config = {
        "target.zip": os.environ.get("SIGNED_ZIP"),
        "setup.exe": os.environ.get("SIGNED_SETUP"),
        "mar.exe": os.environ.get("UNSIGNED_MAR"),
    }
repackage_config = [[
        "installer",
        "--package", "{abs_work_dir}\\inputs\\target.zip",
        "--tag", "{abs_mozilla_dir}\\browser\\installer\\windows\\app.tag",
        "--setupexe", "{abs_work_dir}\\inputs\\setup.exe",
        "-o", "{output_home}\\target.installer.exe"
    ], [
        "mar",
        "-i", "{abs_work_dir}\\inputs\\target.zip",
        "--mar", "{abs_work_dir}\\inputs\\mar.exe",
        "-o", "{output_home}\\target.complete.mar"
    ]]

if '64' in platform.machine():
    plat = "win64"
else:
    plat = "win32"
    # Extend entries for stub installer, only built on win32
    download_config["setup-stub.exe"] = os.environ.get("SIGNED_SETUP_STUB")
    repackage_config.append([
        "installer",
        "--tag", "{abs_mozilla_dir}\\browser\\installer\\windows\\stub.tag",
         "--setupexe", "{abs_work_dir}\\inputs\\setup-stub.exe",
         "-o", "{output_home}\\target.stub-installer.exe"
    ])

config = {
    "input_home": "{abs_work_dir}\\inputs",
    "src_mozconfig": "browser/config/mozconfigs/{}/repack".format(plat),

    "download_config": download_config,

    "repackage_config": repackage_config,

    # ToolTool
    "tooltool_manifest_src": 'browser\\config\\tooltool-manifests\\{}\\releng.manifest'.format(plat),
    'tooltool_url': 'https://api.pub.build.mozilla.org/tooltool/',
    'tooltool_script': [sys.executable,
                        'C:/mozilla-build/tooltool.py'],
    'tooltool_cache': os.environ.get('TOOLTOOL_CACHE'),
    
    'run_configure': False,
}
