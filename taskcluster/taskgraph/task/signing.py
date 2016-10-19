# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

from . import transform


class SigningTask(transform.TransformTask):
    """
    A task implementing a signing job.  These depend on nightly build jobs and
    sign the artifacts after a build has completed.

    We use a dictionary to create the input to the transforms.
    It will have added to it keys `build-label`, the label for the build task,
    and `build-platform` / `build-type`, its platform and type.
    """

    @classmethod
    def get_inputs(cls, kind, path, config, params, loaded_tasks):
        if (config.get('kind-dependencies', []) != ["build"] and
                config.get('kind-dependencies', []) != ["nightly-l10n"]):
            raise Exception("Signing kinds must depend on builds")
        for task in loaded_tasks:
            if task.kind not in config.get('kind-dependencies'):
                continue
            if not task.attributes.get('nightly'):
                continue
            signing_task = {}
            signing_task['build-label'] = task.label
            signing_task['build-platform'] = task.attributes.get('build_platform')
            signing_task['build-type'] = task.attributes.get('build_type')
            signing_task['build-description'] = signing_task['build-platform']
            signing_task['build-run-on-projects'] = task.attributes.get('run_on_projects')
            signing_task['build-treeherder'] = {}
            if task.attributes.get('l10n_chunk'):
                signing_task['l10n_chunk'] = task.attributes['l10n_chunk']
            yield signing_task
