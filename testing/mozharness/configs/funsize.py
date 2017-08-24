import os

platform = "linux64"

config = {
    "input_home": "{abs_work_dir}/inputs",
    "output_home": "{abs_work_dir}/artifacts{locale}",

    "locale": os.environ.get("LOCALE"),

    # ToolTool
    "tooltool_manifest_src": 'browser/config/tooltool-manifests/{}/releng.manifest'.format(platform),
    "tooltool_url": 'http://relengapi/tooltool/',
    'tooltool_script': ["/builds/tooltool.py"],
    'tooltool_cache': os.environ.get('TOOLTOOL_CACHE'),
}
