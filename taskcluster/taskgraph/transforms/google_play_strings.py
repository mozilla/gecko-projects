# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the push-apk kind into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

import functools

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by, Schema
# from taskgraph.util.scriptworker import
from taskgraph.util.push_apk import fill_labels_tranform, validate_jobs_schema_transform_partial

from voluptuous import Required


transforms = TransformSequence()

google_play_description_schema = Schema({
    # the dependent task (object) for this beetmover job, used to inform beetmover.
    Required('name'): basestring,
    Required('label'): basestring,
    Required('description'): basestring,
    Required('job-from'): basestring,
    Required('attributes'): object,
    Required('treeherder'): object,
    Required('run-on-projects'): list,
    Required('worker-type'): basestring,
    Required('worker'): object,
})

validate_jobs_schema_transform = functools.partial(
    validate_jobs_schema_transform_partial,
    google_play_description_schema,
    'GooglePlayStrings'
)

transforms.add(fill_labels_tranform)
transforms.add(validate_jobs_schema_transform)




@transforms.add
def set_worker_data(config, jobs):
    for job in jobs:
        worker = job['worker']

        env = worker.setdefault('env', {})
        resolve_keyed_by(
            env, 'PACKAGE_NAME', item_name=job['name'],
            project=config.params['project']
        )

        yield job
