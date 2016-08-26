/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var fs = require("fs");
var path = require("path");
var merge = require("merge");
var yaml = require("js-yaml");
var slugid = require("slugid");
var flatmap = require("flatmap");
var try_syntax = require("./try_syntax");

// Default values for debugging.
var TC_OWNER = process.env.TC_OWNER || "{{tc_owner}}";
var TC_SOURCE = process.env.TC_SOURCE || "{{tc_source}}";
var TC_PROJECT = process.env.TC_PROJECT || "{{tc_project}}";
var TC_COMMENT = process.env.TC_COMMENT || "{{tc_comment}}";
var NSS_PUSHLOG_ID = process.env.NSS_PUSHLOG_ID || "{{nss_pushlog_id}}";
var NSS_HEAD_REVISION = process.env.NSS_HEAD_REVISION || "{{nss_head_rev}}";

// Register custom YAML types.
var YAML_SCHEMA = yaml.Schema.create([
  // Point in time at $now + x hours.
  new yaml.Type('!from_now', {
    kind: "scalar",

    resolve: function (data) {
      return true;
    },

    construct: function (data) {
      var d = new Date();
      d.setHours(d.getHours() + (data|0));
      return d.toJSON();
    }
  }),

  // Environment variables.
  new yaml.Type('!env', {
    kind: "scalar",

    resolve: function (data) {
      return true;
    },

    construct: function (data) {
      return process.env[data] || "{{" + data.toLowerCase() + "}}";
    }
  })
]);

// Parse a given YAML file.
function parseYamlFile(file, fallback) {
  // Return fallback if the file doesn't exist.
  if (!fs.existsSync(file) && fallback) {
    return fallback;
  }

  // Otherwise, read the file or fail.
  var source = fs.readFileSync(file, "utf-8");
  return yaml.load(source, {schema: YAML_SCHEMA});
}

// Add base information to the given task.
function decorateTask(task) {
  // Assign random task id.
  task.taskId = slugid.v4();

  // TreeHerder routes.
  task.task.routes = [
    "tc-treeherder-stage.v2." + TC_PROJECT + "." + NSS_HEAD_REVISION + "." + NSS_PUSHLOG_ID,
    "tc-treeherder.v2." + TC_PROJECT + "." + NSS_HEAD_REVISION + "." + NSS_PUSHLOG_ID
  ];
}

// Generate all tasks for a given build.
function generateBuildTasks(platform, file) {
  var dir = path.join(__dirname, "./" + platform);

  // Parse base definitions.
  var buildBase = parseYamlFile(path.join(dir, "_build_base.yml"), {});
  var testBase = parseYamlFile(path.join(dir, "_test_base.yml"), {});

  return flatmap(parseYamlFile(path.join(dir, file)), function (task) {
    // Merge base build task definition with the current one.
    var tasks = [task = merge.recursive(true, buildBase, task)];

    // Add base info.
    decorateTask(task);

    // Generate test tasks.
    if (task.tests) {
      // The base definition for all tests of this platform.
      var base = merge.recursive(true, {
        requires: [task.taskId],

        task: {
          payload: {
            env: {
              TC_PARENT_TASK_ID: task.taskId
            }
          }
        }
      }, testBase);

      // Generate and append test task definitions.
      tasks = tasks.concat(flatmap(task.tests, function (name) {
        return generateTestTasks(name, base, task);
      }));

      // |tests| is not part of the schema.
      delete task.tests;
    }

    return tasks;
  });
}

// Generate all tasks for a given test.
function generateTestTasks(name, base, task) {
  // Load test definitions.
  var dir = path.join(__dirname, "./tests");
  var tests = parseYamlFile(path.join(dir, name + ".yml"));

  return tests.map(function (test) {
    // Merge test with base definition.
    test = merge.recursive(true, base, test);

    // Add base info.
    decorateTask(test);

    // We only want to carry over environment variables...
    test.task.payload.env =
      merge.recursive(true, task.task.payload.env,
                            test.task.payload.env);

    // ...and TreeHerder configuration data.
    test.task.extra.treeherder =
      merge.recursive(true, task.task.extra.treeherder,
                            test.task.extra.treeherder);

    return test;
  });
}

// Generate all tasks for a given platform.
function generatePlatformTasks(platform) {
  var dir = path.join(__dirname, "./" + platform);
  var buildBase = parseYamlFile(path.join(dir, "_build_base.yml"), {});
  var testBase = parseYamlFile(path.join(dir, "_test_base.yml"), {});

  // Parse all build tasks.
  return flatmap(fs.readdirSync(dir), function (file) {
    if (!file.startsWith("_") && file.endsWith(".yml")) {
      var tasks = generateBuildTasks(platform, file);

      // Convert env variables to strings.
      tasks.forEach(function (task) {
        var env = task.task.payload.env || {};
        Object.keys(env).forEach(function (name) {
          if (typeof(env[name]) != "undefined") {
            env[name] = env[name] + "";
          }
        });
      });

      return tasks;
    }
  });
}

// Construct the task graph.
var graph = {
  tasks: flatmap(["linux", "windows", "arm", "tools"], generatePlatformTasks)
};

// Filter tasks when try syntax is given.
if (TC_PROJECT == "nss-try") {
  graph.tasks = try_syntax.filterTasks(graph.tasks, TC_COMMENT);
}

// Output the final graph.
process.stdout.write(JSON.stringify(graph, null, 2));
