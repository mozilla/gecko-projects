config = {
    'default_actions': [
        'clobber',
        'checkout-sources',
        'package-source',
    ],
    'stage_platform': 'source',  # Not used, but required by the script
    'buildbot_json_path': 'buildprops.json',
    'app_ini_path': 'FAKE',  # Not used, but required by the script
    'env': {
        'TINDERBOX_OUTPUT': '1',
        'LC_ALL': 'C',
    },
    'src_mozconfig': 'browser/config/mozconfigs/linux64/source',
}
