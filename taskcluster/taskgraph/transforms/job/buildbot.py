# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""

Support for running jobs via buildbot.

"""

from __future__ import absolute_import, print_function, unicode_literals
import slugid

from taskgraph.util.schema import Schema
from taskgraph.util.scriptworker import get_release_config
from voluptuous import Required, Any

from taskgraph.transforms.job import run_job_using

buildbot_run_schema = Schema({
    Required('using'): 'buildbot',

    # the buildername to use for buildbot-bridge, will expand {branch} in name from
    # the current project.
    Required('buildername'): basestring,

    # the product to use
    Required('product'): Any('firefox', 'mobile', 'devedition', 'thunderbird'),
})


@run_job_using('buildbot-bridge', 'buildbot', schema=buildbot_run_schema)
def mozharness_on_buildbot_bridge(config, job, taskdesc):
    run = job['run']
    worker = taskdesc['worker']
    branch = config.params['project']
    product = run['product']

    buildername = run['buildername'].format(branch=branch)
    revision = config.params['head_rev']

    props = {
        'product': product,
        'who': config.params['owner'],
        'upload_to_task_id': slugid.nice(),
    }
    release_config = get_release_config(config)
    if release_config:
        props.update({
            'mozharness_changeset': revision,
            'revision': revision,
        })
        release_config['build_number'] = str(release_config['build_number'])
        props.update(release_config)

    worker.update({
        'buildername': buildername,
        'sourcestamp': {
            'branch': branch,
            'repository': config.params['head_repository'],
            'revision': revision,
        },
    })
    worker.setdefault('properties', {}).update(props)
