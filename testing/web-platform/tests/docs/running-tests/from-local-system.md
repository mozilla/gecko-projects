# Running Tests from the Local System

The tests are designed to be run from your local computer.

## System Setup

The test environment requires [Python 2.7+](http://www.python.org/downloads)
(but not Python 3.x).

On Windows, be sure to add the Python directory (`c:\python2x`, by default) to
your `%Path%` [Environment Variable](http://www.computerhope.com/issues/ch000549.htm),
and read the [Windows Notes](#windows-notes) section below.

<!--
  There does not appear to be a cross-platform means of installing `pip`.
  https://github.com/web-platform-tests/wpt/pull/16670
-->

Install `pip`. On many systems, this can be achieved with the command `python
-m ensurepip`. If this is not possible, use your system's package manager to
install the `python-pip` package.

Next, install `virtualenv` using the following command:

```bash
pip install virtualenv
```

To get the tests running, you need to set up the test domains in your
[`hosts` file](http://en.wikipedia.org/wiki/Hosts_%28file%29%23Location_in_the_file_system).

The necessary content can be generated with `./wpt make-hosts-file`; on
Windows, you will need to preceed the prior command with `python` or
the path to the Python binary (`python wpt make-hosts-file`).

For example, on most UNIX-like systems, you can setup the hosts file with:

```bash
./wpt make-hosts-file | sudo tee -a /etc/hosts
```

And on Windows (this must be run in a PowerShell session with Administrator privileges):

```bash
python wpt make-hosts-file | Out-File %SystemRoot%\System32\drivers\etc\hosts -Encoding ascii -Append
```

If you are behind a proxy, you also need to make sure the domains above are
excluded from your proxy lookups.

[The Ahem font](../writing-tests/ahem) is used to test precise rendering
behavior. This font should be loaded as a web font in tests, using the
`/fonts/ahem.css` stylesheet, as follows:

```
<link rel="stylesheet" type="text/css" href="/fonts/ahem.css" />
```


### Windows Notes

Generally Windows Subsystem for Linux will provide the smoothest user
experience for running web-platform-tests on Windows.

The standard Windows shell requires that all `wpt` commands are prefixed
by the Python binary i.e. assuming `python` is on your path the server is
started using:

`python wpt serve`

## Via the browser

The test environment can then be started using

    ./wpt serve

This will start HTTP servers on two ports and a websockets server on
one port. By default the web servers start on ports 8000 and 8443 and the other
ports are randomly-chosen free ports. Tests must be loaded from the
*first* HTTP server in the output. To change the ports,
create a `config.json` file in the wpt root directory, and add
port definitions of your choice e.g.:

```
{
  "ports": {
    "http": [1234, "auto"],
    "https":[5678]
  }
}
```

After your `hosts` file is configured, the servers will be locally accessible at:

http://web-platform.test:8000/<br>
https://web-platform.test:8443/ *

To use the web-based runner point your browser to:

http://web-platform.test:8000/tools/runner/index.html<br>
https://web-platform.test:8443/tools/runner/index.html *

This server has all the capabilities of the publicly-deployed version--see
[Running the Tests from the Web](from-web).

\**See [Trusting Root CA](../tools/certs/README.md)*

## Via the command line

Many tests can be automatically executed in a new browser instance using

    ./wpt run [browsername] [tests]

This will automatically load the tests in the chosen browser and extract the
test results. For example to run the `dom/historical.html` tests in a local
copy of Chrome:

    ./wpt run chrome dom/historical.html

Or to run in a specified copy of Firefox:

    ./wpt run --binary ~/local/firefox/firefox firefox dom/historical.html

For details on the supported products and a large number of other options for
customising the test run:

    ./wpt run --help

[A complete listing of the command-line arguments is available
here](command-line-arguments).

```eval_rst
.. toctree::
   :hidden:

   command-line-arguments
```

Additional browser-specific documentation:

```eval_rst
.. toctree::

  chrome
  chrome_android
  android_webview
  safari
  webkitgtk_minibrowser
```

For use in continuous integration systems, and other scenarios where regression
tracking is required, the command-line interface supports storing and loading
the expected result of each test in a test run. See [Expectations
Data](../../tools/wptrunner/docs/expectation) for more information on creating
and maintaining these files.
