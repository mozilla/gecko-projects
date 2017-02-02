# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals
from taskgraph import try_option_syntax

INTEGRATION_PROJECTS = set([
    'mozilla-inbound',
    'autoland',
])

RELEASE_PROJECTS = set([
    'mozilla-central',
    'mozilla-aurora',
    'mozilla-beta',
    'mozilla-release',
])

_target_task_methods = {}


def _target_task(name):
    def wrap(func):
        _target_task_methods[name] = func
        return func
    return wrap


def get_method(method):
    """Get a target_task_method to pass to a TaskGraphGenerator."""
    return _target_task_methods[method]


@_target_task('try_option_syntax')
def target_tasks_try_option_syntax(full_task_graph, parameters):
    """Generate a list of target tasks based on try syntax in
    parameters['message'] and, for context, the full task graph."""
    options = try_option_syntax.TryOptionSyntax(parameters['message'], full_task_graph)
    target_tasks_labels = [t.label for t in full_task_graph.tasks.itervalues()
                           if options.task_matches(t.attributes)]

    # If the developer wants test jobs to be rebuilt N times we add that value here
    if int(options.trigger_tests) > 1:
        for l in target_tasks_labels:
            task = full_task_graph[l]
            if 'unittest_suite' in task.attributes:
                task.attributes['task_duplicates'] = options.trigger_tests

    # Add notifications here as well
    if options.notifications:
        for task in full_task_graph:
            owner = parameters.get('owner')
            routes = task.task.setdefault('routes', [])
            if options.notifications == 'all':
                routes.append("notify.email.{}.on-any".format(owner))
            elif options.notifications == 'failure':
                routes.append("notify.email.{}.on-failed".format(owner))
                routes.append("notify.email.{}.on-exception".format(owner))

    return target_tasks_labels


@_target_task('default')
def target_tasks_default(full_task_graph, parameters):
    """Target the tasks which have indicated they should be run on this project
    via the `run_on_projects` attributes."""
    def filter(task):
        run_on_projects = set(task.attributes.get('run_on_projects', []))
        if 'all' in run_on_projects:
            return True
        project = parameters['project']
        if 'integration' in run_on_projects:
            if project in INTEGRATION_PROJECTS:
                return True
        if 'release' in run_on_projects:
            if project in RELEASE_PROJECTS:
                return True
        return project in run_on_projects
    return [l for l, t in full_task_graph.tasks.iteritems() if filter(t)]


@_target_task('ash_tasks')
def target_tasks_ash(full_task_graph, parameters):
    """Target tasks that only run on the ash branch."""
    def filter(task):
        platform = task.attributes.get('build_platform')
        # only select platforms
        if platform not in ('linux64', 'linux64-asan', 'linux64-pgo'):
            return False
        # and none of this linux64-asan/debug stuff
        if platform == 'linux64-asan' and task.attributes['build_type'] == 'debug':
            return False
        # no non-e10s tests
        if task.attributes.get('unittest_suite'):
            if not task.attributes.get('e10s'):
                return False
            # don't run talos on ash
            if task.attributes.get('unittest_suite') == 'talos':
                return False
        # don't upload symbols
        if task.attributes['kind'] == 'upload-symbols':
            return False
        return True
    return [l for l, t in full_task_graph.tasks.iteritems() if filter(t)]


@_target_task('cedar_tasks')
def target_tasks_cedar(full_task_graph, parameters):
    """Target tasks that only run on the cedar branch."""
    def filter(task):
        platform = task.attributes.get('build_platform')
        # only select platforms
        if platform not in ['linux64']:
            return False
        if task.attributes.get('unittest_suite'):
            if not (task.attributes['unittest_suite'].startswith('mochitest')
                    or 'xpcshell' in task.attributes['unittest_suite']):
                return False
        return True
    return [l for l, t in full_task_graph.tasks.iteritems() if filter(t)]


@_target_task('graphics_tasks')
def target_tasks_graphics(full_task_graph, parameters):
    """In addition to doing the filtering by project that the 'default'
       filter does, also remove artifact builds because we have csets on
       the graphics branch that aren't on the candidate branches of artifact
       builds"""
    filtered_for_project = target_tasks_default(full_task_graph, parameters)

    def filter(task):
        if task.attributes['kind'] == 'artifact-build':
            return False
        return True
    return [l for l in filtered_for_project if filter(full_task_graph[l])]


@_target_task('nightly_fennec')
def target_tasks_nightly(full_task_graph, parameters):
    """Select the set of tasks required for a nightly build of fennec. The
    nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    def filter(task):
        platform = task.attributes.get('build_platform')
        if platform in ('android-api-15-nightly', 'android-x86-nightly'):
            return task.attributes.get('nightly', False)
    return [l for l, t in full_task_graph.tasks.iteritems() if filter(t)]


@_target_task('nightly_linux')
def target_tasks_nightly_linux(full_task_graph, parameters):
    """Select the set of tasks required for a nightly build of linux. The
    nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    def filter(task):
        platform = task.attributes.get('build_platform')
        if platform in ('linux64-nightly', 'linux-nightly'):
            return task.attributes.get('nightly', False)
    return [l for l, t in full_task_graph.tasks.iteritems() if filter(t)]


@_target_task('stylo_tasks')
def target_tasks_stylo(full_task_graph, parameters):
    """Target stylotasks that only run on the m-c branch."""
    def filter(task):
        platform = task.attributes.get('build_platform')
        # only select platforms
        if platform not in ('linux64-stylo'):
            return False
        return True
    return [l for l, t in full_task_graph.tasks.iteritems() if filter(t)]

# nightly_linux should be refactored to be nightly_all once
# https://bugzilla.mozilla.org/show_bug.cgi?id=1267425 dependent bugs are implemented
@_target_task('nightly_macosx')
def target_tasks_nightly_macosx(full_task_graph, parameters):
    """Select the set of tasks required for a nightly build of linux. The
    nightly build process involves a pipeline of builds, signing,
    and, eventually, uploading the tasks to balrog."""
    def filter(task):
        platform = task.attributes.get('build_platform')
        if platform in ('macosx64-nightly', ):
            return task.attributes.get('nightly', False)
    return [l for l, t in full_task_graph.tasks.iteritems() if filter(t)]
