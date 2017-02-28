# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

import logging
import os
import urllib2

from . import base
from .. import GECKO
from taskgraph.util.docker import (
    docker_image,
    generate_context_hash,
    INDEX_PREFIX,
)
from taskgraph.util.taskcluster import get_artifact_url
from taskgraph.util.templates import Templates

logger = logging.getLogger(__name__)


class DockerImageTask(base.Task):

    @classmethod
    def load_tasks(cls, kind, path, config, params, loaded_tasks):
        parameters = {
            'pushlog_id': params.get('pushlog_id', 0),
            'pushdate': params['moz_build_date'],
            'pushtime': params['moz_build_date'][8:],
            'year': params['moz_build_date'][0:4],
            'month': params['moz_build_date'][4:6],
            'day': params['moz_build_date'][6:8],
            'project': params['project'],
            'docker_image': docker_image,
            'base_repository': params['base_repository'] or params['head_repository'],
            'head_repository': params['head_repository'],
            'head_ref': params['head_ref'] or params['head_rev'],
            'head_rev': params['head_rev'],
            'owner': params['owner'],
            'level': params['level'],
            'source': '{repo}file/{rev}/taskcluster/ci/docker-image/image.yml'
                      .format(repo=params['head_repository'], rev=params['head_rev']),
            'index_image_prefix': INDEX_PREFIX,
            'artifact_path': 'public/image.tar.zst',
        }

        tasks = []
        templates = Templates(path)
        for image_name, image_symbol in config['images'].iteritems():
            context_path = os.path.join('taskcluster', 'docker', image_name)
            context_hash = generate_context_hash(GECKO, context_path, image_name)

            image_parameters = dict(parameters)
            image_parameters['image_name'] = image_name
            image_parameters['context_hash'] = context_hash

            image_task = templates.load('image.yml', image_parameters)
            attributes = {'image_name': image_name}

            # unique symbol for different docker image
            if 'extra' in image_task['task']:
                image_task['task']['extra']['treeherder']['symbol'] = image_symbol

            # As an optimization, if the context hash exists for a high level, that image
            # task ID will be used.  The reasoning behind this is that eventually everything ends
            # up on level 3 at some point if most tasks use this as a common image
            # for a given context hash, a worker within Taskcluster does not need to contain
            # the same image per branch.
            index_paths = ['{}.level-{}.{}.hash.{}'.format(
                                INDEX_PREFIX, level, image_name, context_hash)
                           for level in reversed(range(int(params['level']), 4))]

            tasks.append(cls(kind, 'build-docker-image-' + image_name,
                             task=image_task['task'], attributes=attributes,
                             index_paths=index_paths))

        return tasks

    def get_dependencies(self, taskgraph):
        return []

    def optimize(self, params):
        optimized, taskId = super(DockerImageTask, self).optimize(params)
        if optimized and taskId:
            try:
                # Only return the task ID if the artifact exists for the indexed
                # task.
                request = urllib2.Request(get_artifact_url(
                    taskId, 'public/image.tar.zst',
                    use_proxy=bool(os.environ.get('TASK_ID'))))
                request.get_method = lambda: 'HEAD'
                urllib2.urlopen(request)

                # HEAD success on the artifact is enough
                return True, taskId
            except urllib2.HTTPError:
                pass

        return False, None

    @classmethod
    def from_json(cls, task_dict):
        # Generating index_paths for optimization
        imgMeta = task_dict['task']['extra']['imageMeta']
        image_name = imgMeta['imageName']
        context_hash = imgMeta['contextHash']
        index_paths = ['{}.level-{}.{}.hash.{}'.format(
                            INDEX_PREFIX, level, image_name, context_hash)
                       for level in reversed(range(int(imgMeta['level']), 4))]
        docker_image_task = cls(kind='docker-image',
                                label=task_dict['label'],
                                attributes=task_dict['attributes'],
                                task=task_dict['task'],
                                index_paths=index_paths)
        return docker_image_task
