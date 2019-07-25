# Test Suite Design

The vast majority of the test suite is formed of HTML pages, which can
be loaded in a browser and either programmatically provide a result or
provide a set of steps to run the test and obtain the result.

The tests are, in general, short, cross-platform, and self-contained,
and should be easy to run in any browser.


## Test Layout

Each top level directory in the repository corresponds to tests for a
single specification, with the exception of `css/` which contains
test suites for CSS WG specifications. For W3C specs, these directories
are typically named after the shortname of the spec (i.e. the name
used for snapshot publications under `/TR/`); for WHATWG specs, they
are typically named after the subdomain of the spec (i.e. trimming
`.spec.whatwg.org` from the URL); for other specs, something deemed
sensible is used. In any case, there are occasional exceptions for
historic reasons.

Within the specification-specific directory there are two common ways
of laying out tests: the first is a flat structure which is sometimes
adopted for very short specifications; the alternative is a nested
structure with each subdirectory corresponding to the id of a heading
in the specification. The latter provides some implicit metadata about
the part of a specification being tested according to its location in
the filesystem, and is preferred for larger specifications.


## Test Types

Tests in this project use a few different approaches to verify expected
behavior. The tests can be classified based on the way they express
expectations:

* Rendering tests ensure that the browser graphically displays pages as
  expected. There are a few different ways this is done:

  * [Reftests][] render two (or more) web pages and combine them with equality
    assertions about their rendering (e.g., `A.html` and `B.html` must render
    identically), run either by the user switching between tabs/windows and
    trying to observe differences or through [automated
    scripts][running-from-local-system].

  * [Visual tests][visual] display a page where the result is determined either
    by a human looking at it or by comparing it with a saved screenshot for
    that user agent on that platform.

* [testharness.js][] tests verify that JavaScript interfaces behave as
  expected. They get their name from the JavaScript harness that's used to
  execute them.

* [wdspec][] tests are written in Python and test [the WebDriver browser
  automation protocol](https://w3c.github.io/webdriver/)

* [Manual tests][manual] rely on a human to run them and determine their
  result.

[reftests]: writing-tests/reftests
[testharness.js]: writing-tests/testharness
[visual]: writing-tests/visual
[manual]: writing-tests/manual
[running-from-local-system]: running-tests/from-local-system
[wdspec]: writing-tests/wdspec
