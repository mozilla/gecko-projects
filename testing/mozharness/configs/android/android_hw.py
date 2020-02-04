# mozharness configuration for Android hardware unit tests
#
# This configuration should be combined with suite definitions and other
# mozharness configuration from android_common.py, or similar.

config = {
    "exes": {},
    "env": {
        "DISPLAY": ":0.0",
        "PATH": "%(PATH)s",
    },
    "default_actions": [
        'clobber',
        'download-and-extract',
        'create-virtualenv',
        'verify-device',
        'install',
        'run-tests',
    ],
    "tooltool_cache": "/builds/tooltool_cache",
    # from android_common.py
    "download_tooltool": True,
    "minidump_stackwalk_path": "linux64-minidump_stackwalk",
    "minidump_tooltool_manifest_path": "config/tooltool-manifests/linux64/releng.manifest",
    "xpcshell_extra": "--remoteTestRoot=/data/local/tests",
}
