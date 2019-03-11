# -*- coding: utf-8 -*-

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

import logging

from taskgraph.util.taskcluster import (
    status_task,
    rerun_task
)
from .registry import register_callback_action
from .util import fetch_graph_and_labels
from taskgraph.util import taskcluster

logger = logging.getLogger(__name__)

RERUN_STATES = ('exception', 'failed')


@register_callback_action(
    title='Rerun',
    name='rerun',
    generic=True,
    symbol='rr',
    description=(
        'Rerun a task.\n\n'
        'This only works on failed or exception tasks in the original taskgraph,'
        ' and is CoT friendly.'
    ),
    order=300,
    context=[{}],
    schema={
        'type': 'object',
        'properties': {}
    }
)
def rerun_action(parameters, graph_config, input, task_group_id, task_id):
    task = taskcluster.get_task_definition(task_id)
    parameters = dict(parameters)
    decision_task_id, full_task_graph, label_to_taskid = fetch_graph_and_labels(
        parameters, graph_config)
    label = task['metadata']['name']
    if task_id not in label_to_taskid.values():
        logger.error(
            "Refusing to rerun {}: taskId {} not in decision task {} label_to_taskid!".format(
                label, task_id, decision_task_id
            )
        )

    status = status_task(task_id)
    if status not in RERUN_STATES:
        logger.warning(
            "No need to to rerun {}: state '{}' not in {}!".format(label, status, RERUN_STATES)
        )
        return
    rerun_task(task_id)
    logger.info('Reran {}'.format(label))
