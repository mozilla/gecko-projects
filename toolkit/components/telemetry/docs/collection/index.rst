===============
Data collection
===============

There are different APIs and formats to collect data in Firefox, all suiting different use cases.

In general, we aim to submit data in a common format where possible. This has several advantages; from common code and tooling to sharing analysis know-how.

In cases where this isn't possible and more flexibility is needed, we can submit custom pings or consider adding different data formats to existing pings.

*Note:* Every new data collection must go through a `data collection review <https://wiki.mozilla.org/Firefox/Data_Collection>`_.

The current data collection possibilities include:

* :doc:`scalars` allow recording of a single value (string, boolean, a number)
* :doc:`histograms` can efficiently record multiple data points
* ``environment`` data records information about the system and settings a session occurs in
* :doc:`events` can record richer data on individual occurences of specific actions
* ``TelemetryLog`` allows collecting ordered event entries (note: this does not have supporting analysis tools)
* :doc:`measuring elapsed time <measuring-time>`
* :doc:`custom pings <custom-pings>`
* :doc:`stack capture <stack-capture>` allow recording application call stacks
* :doc:`Use counters <use-counters>` measure the usage of web platform features

.. toctree::
   :maxdepth: 2
   :titlesonly:
   :hidden:
   :glob:

   scalars
   histograms
   events
   measuring-time
   custom-pings
   stack-capture
   *

Browser Usage Telemetry
~~~~~~~~~~~~~~~~~~~~~~~
For more information, see :ref:`browserusagetelemetry`.

Experiment Annotation
~~~~~~~~~~~~~~~~~~~~~
Experiment annotations can be added through the API exposed in ``TelemetryEnvironment.jsm`` and are collected in the :doc:`environment <../data/environment>`:

- ``TelemetryEnvironment.setExperimentActive(id, branch)``, adds an annotation to the environment for the provided ``id`` and ``branch``. This triggers a new subsession.
- ``TelemetryEnvironment.setExperimentInactive(id)``, removes the annotation for the experiment with the provided ``id``. This triggers a new subsession.
- ``TelemetryEnvironment.getActiveExperiments()``, returns a dictionary containing the informations for each active experiment.
