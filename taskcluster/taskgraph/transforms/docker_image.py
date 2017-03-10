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
    INDEX_PREFIX,
)

transforms = TransformSequence()

ROUTE_TEMPLATES = [
    'index.{index_prefix}.level-{level}.{image_name}.latest',
    'index.{index_prefix}.level-{level}.{image_name}.pushdate.{year}.{month}-{day}-{pushtime}',
    'index.{index_prefix}.level-{level}.{image_name}.hash.{context_hash}',
]


@transforms.add
def fill_template(config, tasks):
    for task in tasks:
        image_name = task.pop('name')
        job_symbol = task.pop('symbol')

        context_path = os.path.join('taskcluster', 'docker', image_name)
        context_hash = generate_context_hash(GECKO, context_path, image_name)

        description = 'Build the docker image {} for use by dependent tasks'.format(
            image_name)

        routes = []
        for tpl in ROUTE_TEMPLATES:
            routes.append(tpl.format(
                index_prefix=INDEX_PREFIX,
                level=config.params['level'],
                image_name=image_name,
                project=config.params['project'],
                head_rev=config.params['head_rev'],
                pushlog_id=config.params.get('pushlog_id', 0),
                pushtime=config.params['moz_build_date'][8:],
                year=config.params['moz_build_date'][0:4],
                month=config.params['moz_build_date'][4:6],
                day=config.params['moz_build_date'][6:8],
                context_hash=context_hash,
            ))

        # As an optimization, if the context hash exists for a high level, that image
        # task ID will be used.  The reasoning behind this is that eventually everything ends
        # up on level 3 at some point if most tasks use this as a common image
        # for a given context hash, a worker within Taskcluster does not need to contain
        # the same image per branch.
        optimizations = [['index-search', '{}.level-{}.{}.hash.{}'.format(
            INDEX_PREFIX, level, image_name, context_hash)]
            for level in reversed(range(int(config.params['level']), 4))]

        # include some information that is useful in reconstructing this task
        # from JSON
        extra = {
            'imageMeta': {
                'level': config.params['level'],
                'contextHash': context_hash,
                'imageName': image_name,
            },
        }

        taskdesc = {
            'label': 'build-docker-image-' + image_name,
            'description': description,
            'attributes': {'image_name': image_name},
            'expires-after': '1 year',
            'routes': routes,
            'optimizations': optimizations,
            'scopes': ['secrets:get:project/taskcluster/gecko/hgfingerprint'],
            'extra': extra,
            'treeherder': {
                'symbol': job_symbol,
                'platform': 'taskcluster-images/opt',
                'kind': 'other',
                'tier': 1,
            },
            'run-on-projects': [],
            'worker-type': 'aws-provisioner-v1/gecko-images',
            # can't use {in-tree: ..} here, otherwise we might try to build
            # this image..
            'worker': {
                'implementation': 'docker-worker',
                'docker-image': docker_image('image_builder'),
                'caches': [{
                    'type': 'persistent',
                    'name': 'level-{}-imagebuilder-v1'.format(config.params['level']),
                    'mount-point': '/home/worker/checkouts',
                }],
                'artifacts': [{
                    'type': 'file',
                    'path': '/home/worker/workspace/artifacts/image.tar.zst',
                    'name': 'public/image.tar.zst',
                }],
                'env': {
                    'HG_STORE_PATH': '/home/worker/checkouts/hg-store',
                    'HASH': context_hash,
                    'PROJECT': config.params['project'],
                    'IMAGE_NAME': image_name,
                    'GECKO_BASE_REPOSITORY': config.params['base_repository'],
                    'GECKO_HEAD_REPOSITORY': config.params['head_repository'],
                    'GECKO_HEAD_REV': config.params['head_rev'],
                },
                'chain-of-trust': True,
                'docker-in-docker': True,
                'taskcluster-proxy': True,
                'max-run-time': 3600,
            },
        }

        yield taskdesc
