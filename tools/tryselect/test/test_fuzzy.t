  $ . $TESTDIR/setup.sh
  $ cd $topsrcdir

Test fuzzy selector

  $ ./mach try fuzzy $testargs -q "'foo"
  Commit message:
  Fuzzy query='foo
  
  Pushed via `mach try fuzzy`
  Calculated try_task_config.json:
  {
      "tasks": [
          "test/foo-debug",
          "test/foo-opt"
      ],
      "templates": {
          "env": {
              "TRY_SELECTOR": "fuzzy"
          }
      },
      "version": 1
  }
  
  $ ./mach try fuzzy $testargs -q "'bar"
  no tasks selected
  $ ./mach try fuzzy $testargs --full -q "'bar"
  Commit message:
  Fuzzy query='bar
  
  Pushed via `mach try fuzzy`
  Calculated try_task_config.json:
  {
      "tasks": [
          "test/bar-debug",
          "test/bar-opt"
      ],
      "templates": {
          "env": {
              "TRY_SELECTOR": "fuzzy"
          }
      },
      "version": 1
  }
  

Test multiple selectors

  $ ./mach try fuzzy $testargs --full -q "'foo" -q "'bar"
  Commit message:
  Fuzzy query='foo&query='bar
  
  Pushed via `mach try fuzzy`
  Calculated try_task_config.json:
  {
      "tasks": [
          "test/bar-debug",
          "test/bar-opt",
          "test/foo-debug",
          "test/foo-opt"
      ],
      "templates": {
          "env": {
              "TRY_SELECTOR": "fuzzy"
          }
      },
      "version": 1
  }
  

Test query intersection

  $ ./mach try fuzzy $testargs --and -q "'foo" -q "'opt"
  Commit message:
  Fuzzy query='foo&query='opt
  
  Pushed via `mach try fuzzy`
  Calculated try_task_config.json:
  {
      "tasks": [
          "test/foo-opt"
      ],
      "templates": {
          "env": {
              "TRY_SELECTOR": "fuzzy"
          }
      },
      "version": 1
  }
  


Test exact match

  $ ./mach try fuzzy $testargs --full -q "testfoo | 'testbar"
  Commit message:
  Fuzzy query=testfoo | 'testbar
  
  Pushed via `mach try fuzzy`
  Calculated try_task_config.json:
  {
      "tasks": [
          "test/foo-debug",
          "test/foo-opt"
      ],
      "templates": {
          "env": {
              "TRY_SELECTOR": "fuzzy"
          }
      },
      "version": 1
  }
  
  $ ./mach try fuzzy $testargs --full --exact -q "testfoo | 'testbar"
  Commit message:
  Fuzzy query=testfoo | 'testbar
  
  Pushed via `mach try fuzzy`
  Calculated try_task_config.json:
  {
      "tasks": [
          "test/bar-debug",
          "test/bar-opt"
      ],
      "templates": {
          "env": {
              "TRY_SELECTOR": "fuzzy"
          }
      },
      "version": 1
  }
  


Test templates

  $ ./mach try fuzzy --no-push --artifact -q "'foo"
  Commit message:
  Fuzzy query='foo
  
  Pushed via `mach try fuzzy`
  Calculated try_task_config.json:
  {
      "tasks": [
          "test/foo-debug",
          "test/foo-opt"
      ],
      "templates": {
          "artifact": {
              "enabled": "1"
          },
          "env": {
              "TRY_SELECTOR": "fuzzy"
          }
      },
      "version": 1
  }
  
  $ ./mach try fuzzy $testargs --env FOO=1 --env BAR=baz -q "'foo"
  Commit message:
  Fuzzy query='foo
  
  Pushed via `mach try fuzzy`
  Calculated try_task_config.json:
  {
      "tasks": [
          "test/foo-debug",
          "test/foo-opt"
      ],
      "templates": {
          "env": {
              "BAR": "baz",
              "FOO": "1",
              "TRY_SELECTOR": "fuzzy"
          }
      },
      "version": 1
  }
  
