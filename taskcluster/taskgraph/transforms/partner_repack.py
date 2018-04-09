# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the partner repack task into an actual task description.
"""

from __future__ import absolute_import, print_function, unicode_literals

from taskgraph.transforms.base import TransformSequence
from taskgraph.util.schema import resolve_keyed_by
from taskgraph.util.scriptworker import get_release_config
from taskgraph.util.partners import get_partner_config_by_kind, check_if_partners_enabled
from taskgraph.util.taskcluster import get_artifact_path


transforms = TransformSequence()

# debugging goop
import logging
log = logging.getLogger(__name__)


transforms.add(check_if_partners_enabled)


@transforms.add
def resolve_properties(config, tasks):
    for task in tasks:
        for property in ("REPACK_MANIFESTS_URL", ):
            property = "worker.env.{}".format(property)
            resolve_keyed_by(task, property, property, **config.params)

        if task['worker']['env']['REPACK_MANIFESTS_URL'].startswith('git@'):
            task.setdefault('scopes', []).append(
                'secrets:get:project/releng/gecko/build/level-{level}/partner-github-ssh'.format(
                    **config.params
                )
            )

        yield task


@transforms.add
def make_label(config, tasks):
    for task in tasks:
        task['label'] = "{}-{}".format(config.kind, task['name'])
        yield task


@transforms.add
def add_command_arguments(config, tasks):
    release_config = get_release_config(config)
    for task in tasks:
        # add the MOZHARNESS_OPTIONS, eg version=61.0, build-number=1, platform=win64
        task['run']['options'] = [
            'version={}'.format(release_config['version']),
            'build-number={}'.format(release_config['build_number']),
            'platform={}'.format(task['attributes']['build_platform'].split('-')[0]),
        ]

        # The upstream taskIds are stored a special environment variable, because we want to use
        # task-reference's to resolve dependencies, but the string handling of MOZHARNESS_OPTIONS
        # blocks that. It's space-separated string of ids in the end.
        task['worker']['env']['UPSTREAM_TASKIDS'] = {
            'task-reference': ' '.join(['<{}>'.format(dep) for dep in task['dependencies']])
        }

        yield task


def generate_platform_artifacts(platform):
    if platform.startswith('win'):
        return ['target.zip', 'setup.exe']
    elif platform.startswith('macosx'):
        return ['target.tar.gz']
    elif platform.startswith('linux'):
        return ['target.tar.bz2']
    else:
        raise ValueError('Unimplemented platform %s'.format(platform))


# seems likely this exists elsewhere already
def get_ftp_platform(platform):
    if platform.startswith('win32'):
        return 'win32'
    elif platform.startswith('win64'):
        return 'win64'
    elif platform.startswith('macosx'):
        return 'mac'
    elif platform.startswith('linux-'):
        return 'linux-i686'
    elif platform.startswith('linux64'):
        return 'linux-x86_64'
    else:
        raise ValueError('Unimplemented platform %s'.format(platform))


# TODO - can we generalise this for all partner tasks ?
@transforms.add
def add_artifacts(config, tasks):
    for task in tasks:
        partner_configs = get_partner_config_by_kind(
            config, config.kind
        )
        platform = task["attributes"]["build_platform"]
        platform_files = generate_platform_artifacts(platform)

        if not task["worker"].get("artifacts"):
            task["worker"]["artifacts"] = []

        for partner, partner_config in partner_configs.iteritems():
            # TODO clean up configs? Some have a {} as the config
            for sub_partner, cfg in partner_config.iteritems():
                if not cfg or platform not in cfg.get("platforms", []):
                    continue
                for locale in cfg.get("locales", []):
                    # Some partner configs have public builds, and specific a path in the
                    # candidates directory
                    if cfg.get('upload_to_candidates') and cfg.get('output_dir'):
                        subst = {
                            'partner': partner,
                            'partner_distro': sub_partner,
                            'locale': locale,
                            'platform': get_ftp_platform(platform)
                        }
                        prefix = get_artifact_path(task,
                                                   cfg['output_dir'] % subst)
                    else:
                        prefix = get_artifact_path(task,
                                                   '{}/{}/{}'.format(partner, sub_partner, locale))
                    worker_prefix = '{}/{}'.format('/builds/worker/workspace/build/artifacts',
                                                   prefix)

                    task["worker"]["artifacts"].extend(
                        [
                            {
                                'name': '{}/{}'.format(prefix, f),
                                'path': '{}/{}'.format(worker_prefix, f),
                                'type': 'file',
                            } for f in platform_files
                        ]
                    )

        yield task
