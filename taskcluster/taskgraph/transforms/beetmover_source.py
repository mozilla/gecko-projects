# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Transform the beetmover-source task to also append `build` as dependency
"""
from __future__ import absolute_import

from taskgraph.transforms.base import TransformSequence

transforms = TransformSequence()


@transforms.add
def tweak_beetmover_source_dependencies_and_upstream_artifacts(config, jobs):
    for job in jobs:
        # HACK: instead of grabbing SOURCE file from `release-source` task, we
        # instead take it along with SOURCE.asc directly from the
        # `release-source-signing`.
        #
        # XXX: this hack should go away by
        # * rewriting beetmover transforms to allow more flexibility in deps

        if job['attributes']['shipping_product'] == 'firefox':
            job['dependencies']['build'] = u'build-linux64-nightly/opt'
        elif job['attributes']['shipping_product'] == 'fennec':
            job['dependencies']['build'] = u'build-android-api-16-nightly/opt'
        elif job['attributes']['shipping_product'] == 'devedition':
            job['dependencies']['build'] = u'build-linux64-devedition-nightly/opt'
        else:
            raise NotImplemented(
                "Unknown shipping_product {} for beetmover_source!".format(
                    job['attributes']['shipping_product']
                )
            )

        yield job
