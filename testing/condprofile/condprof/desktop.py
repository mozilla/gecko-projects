# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
import os
import contextlib

import attr
from arsenic.services import Geckodriver, free_port, subprocess_based_service

from condprof.util import (
    BaseEnv,
    latest_nightly,
    LOG,
    get_version,
    ERROR,
    get_current_platform,
)


@attr.s
class DesktopGeckodriver(Geckodriver):
    async def start(self):
        port = free_port()
        await self._check_version()
        LOG("Running Webdriver on port %d" % port)
        LOG("Running Marionette on port 2828")
        pargs = [self.binary, "-vv", "--port", str(port), "--marionette-port", "2828"]
        return await subprocess_based_service(
            pargs, f"http://localhost:{port}", self.log_file
        )


@contextlib.contextmanager
def dummy_device(*args, **kw):
    yield None


class DesktopEnv(BaseEnv):
    def get_target_platform(self):
        return get_current_platform()

    def get_device(self, *args, **kw):
        return dummy_device(*args, **kw)

    @contextlib.contextmanager
    def get_browser(self):
        with latest_nightly(self.firefox) as binary:
            self.firefox = os.path.abspath(binary)
            if not os.path.exists(self.firefox):
                raise IOError(self.firefox)
            yield

    def get_browser_args(self, headless):
        options = ["-profile", self.profile]
        if headless:
            options.append("-headless")
        args = {"moz:firefoxOptions": {"args": options}}
        if self.firefox is not None:
            args["moz:firefoxOptions"]["binary"] = self.firefox
        return args

    def get_browser_version(self):
        try:
            return get_version(self.firefox)
        except Exception as e:
            ERROR("Could not get Firefox version %s" % str(e))
            return "unknown"

    def get_geckodriver(self, log_file):
        return DesktopGeckodriver(binary=self.geckodriver, log_file=log_file)
