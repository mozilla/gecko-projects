# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the upload-symbols task description template,
  taskcluster/ci/upload-symbols/job-template.yml
into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

import os

from taskgraph.transforms.base import TransformSequence
from .. import GECKO
from taskgraph.util.docker import (
    docker_image,
    generate_context_hash,
)
from taskgraph.util.cached_tasks import add_optimization

transforms = TransformSequence()


@transforms.add
def fill_template(config, tasks):
    for task in tasks:
        image_name = task.pop('name')
        job_symbol = task.pop('symbol')

        context_path = os.path.join('taskcluster', 'docker', image_name)
        context_hash = generate_context_hash(GECKO, context_path, image_name)

        description = 'Build the docker image {} for use by dependent tasks'.format(
            image_name)

        # Adjust the zstandard compression level based on the execution level.
        # We use faster compression for level 1 because we care more about
        # end-to-end times. We use slower/better compression for other levels
        # because images are read more often and it is worth the trade-off to
        # burn more CPU once to reduce image size.
        zstd_level = '3' if int(config.params['level']) == 1 else '10'

        # include some information that is useful in reconstructing this task
        # from JSON
        taskdesc = {
            'label': 'build-docker-image-' + image_name,
            'description': description,
            'attributes': {'image_name': image_name},
            'expires-after': '1 year',
            'scopes': ['secrets:get:project/taskcluster/gecko/hgfingerprint'],
            'treeherder': {
                'symbol': job_symbol,
                'platform': 'taskcluster-images/opt',
                'kind': 'other',
                'tier': 1,
            },
            'run-on-projects': [],
            'worker-type': 'aws-provisioner-v1/gecko-{}-images'.format(
                config.params['level']),
            # can't use {in-tree: ..} here, otherwise we might try to build
            # this image..
            'worker': {
                'implementation': 'docker-worker',
                'os': 'linux',
                'docker-image': docker_image('image_builder'),
                'caches': [{
                    'type': 'persistent',
                    'name': 'level-{}-imagebuilder-v1'.format(config.params['level']),
                    'mount-point': '/builds/worker/checkouts',
                }],
                'volumes': [
                    # Keep in sync with Dockerfile and TASKCLUSTER_VOLUMES
                    '/builds/worker/checkouts',
                    '/builds/worker/workspace',
                ],
                'artifacts': [{
                    'type': 'file',
                    'path': '/builds/worker/workspace/artifacts/image.tar.zst',
                    'name': 'public/image.tar.zst',
                }],
                'env': {
                    'HG_STORE_PATH': '/builds/worker/checkouts/hg-store',
                    'HASH': context_hash,
                    'PROJECT': config.params['project'],
                    'IMAGE_NAME': image_name,
                    'DOCKER_IMAGE_ZSTD_LEVEL': zstd_level,
                    'GECKO_BASE_REPOSITORY': config.params['base_repository'],
                    'GECKO_HEAD_REPOSITORY': config.params['head_repository'],
                    'GECKO_HEAD_REV': config.params['head_rev'],
                    'TASKCLUSTER_VOLUMES': '/builds/worker/checkouts;/builds/worker/workspace',
                },
                'chain-of-trust': True,
                'docker-in-docker': True,
                'taskcluster-proxy': True,
                'max-run-time': 7200,
            },
        }

        add_optimization(
            config, taskdesc,
            cache_type="docker-images.v1",
            cache_name=image_name,
            digest=context_hash,
        )

        yield taskdesc
