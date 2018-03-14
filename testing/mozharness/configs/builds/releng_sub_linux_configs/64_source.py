config = {
    'default_actions': [
        'clobber',
        'checkout-sources',
        'package-source',
    ],
    'objdir': 'obj-firefox',
    'stage_platform': 'source',  # Not used, but required by the script
    'buildbot_json_path': 'buildprops.json',
    'vcs_share_base': '/builds/hg-shared',
    'app_ini_path': 'FAKE',  # Not used, but required by the script
    'env': {
        'HG_SHARE_BASE_DIR': '/builds/hg-shared',
        'TINDERBOX_OUTPUT': '1',
        'LC_ALL': 'C',
    },
    'src_mozconfig': 'browser/config/mozconfigs/linux64/source',
}
