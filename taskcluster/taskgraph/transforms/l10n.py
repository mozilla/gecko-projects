# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
Do transforms specific to l10n kind
"""

from __future__ import absolute_import, print_function, unicode_literals

import copy

from taskgraph.transforms.base import TransformSequence

transforms = TransformSequence()


@transforms.add
def setup_nightly_dependency(config, jobs):
    """ Sets up a task dependency to the signing job this relates to """
    for job in jobs:
        if 'nightly' not in config.kind:
            yield job
            continue  # do not add a dep unless we're a nightly
        dep_label = "build-{}".format(job['name'])
        job['dependencies'] = {'unsigned-build': dep_label}
        yield job


@transforms.add
def chunkify(config, jobs):
    """ Utilizes chunking for l10n stuff """
    for job in jobs:
        chunks = job.get('chunks')
        if 'chunks' in job:
            del job['chunks']
        if chunks:
            for this_chunk in range(1, chunks + 1):
                chunked = copy.deepcopy(job)
                chunked['name'] = chunked['name'].replace(
                    '/', '-{}/'.format(this_chunk), 1
                )
                chunked['run']['options'] = chunked['run'].get('options', [])
                chunked['run']['options'].extend(["total-chunks={}".format(chunks),
                                                  "this-chunk={}".format(this_chunk)])
                chunked['attributes']['l10n_chunk'] = this_chunk
                yield chunked
        else:
            yield job


@transforms.add
def mh_config_replace_project(config, jobs):
    """ Replaces {project} in mh config entries with the current project """
    # XXXCallek This is a bad pattern but exists to satisfy ease-of-porting for buildbot
    for job in jobs:
        if not job['run'].get('using') == 'mozharness':
            # Nothing to do, not mozharness
            yield job
            continue
        job['run']['config'] = map(
            lambda x: x.format(project=config.params['project']),
            job['run']['config']
            )
        yield job


@transforms.add
def mh_options_replace_project(config, jobs):
    """ Replaces {project} in mh option entries with the current project """
    # XXXCallek This is a bad pattern but exists to satisfy ease-of-porting for buildbot
    for job in jobs:
        if not job['run'].get('using') == 'mozharness':
            # Nothing to do, not mozharness
            yield job
            continue
        job['run']['options'] = map(
            lambda x: x.format(project=config.params['project']),
            job['run']['options']
            )
        yield job
