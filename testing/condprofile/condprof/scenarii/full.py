import random
import os
import asyncio

from condprof.util import logger, get_credentials
from condprof.helpers import TabSwitcher, execute_async_script, is_mobile


class Builder:
    def __init__(self, options):
        self.options = options
        self.words = self._read_lines("words.txt")
        self.urls = self._build_url_list(self._read_lines("urls.txt"))
        self.sync_js = "\n".join(self._read_lines("sync.js"))
        self.platform = options.get("platform", "")
        self.mobile = is_mobile(self.platform)
        self.max_urls = options.get("max_urls", 150)

        # see Bug 1608604 - on GV we get OOM killed if we do too much...
        if "gecko" in self.platform:
            self.max_urls = max(self.max_urls, 30)
        # see Bug 1619107 - we have stability issues with Fennec @ bitbar
        elif "fennec" in self.platform:
            self.max_urls = max(self.max_urls, 5)
        elif self.mobile:
            self.max_urls = max(self.max_urls, 150)

        # we're syncing only on desktop for now
        self.syncing = not self.mobile
        if self.syncing:
            self.username, self.password = get_credentials()
            if self.username is None:
                raise ValueError("Sync operations need an FxA username and password")
        else:
            self.username, self.password = None, None

    def _read_lines(self, filename):
        path = os.path.join(os.path.dirname(__file__), filename)
        with open(path) as f:
            return f.readlines()

    def _build_url_list(self, urls):
        url_list = []
        for url in urls:
            url = url.strip()
            if url.startswith("#"):
                continue
            for word in self.words:
                word = word.strip()
                if word.startswith("#"):
                    continue
                url_list.append(url.format(word))
        random.shuffle(url_list)
        return url_list

    async def sync(self, session, metadata):
        if not self.syncing:
            return
        # now that we've visited all pages, we want to upload to FXSync
        logger.info("Syncing profile to FxSync")
        logger.info("Username is %s, password is %s" % (self.username, self.password))
        script_res = await execute_async_script(
            session,
            self.sync_js,
            self.username,
            self.password,
            "https://accounts.stage.mozaws.net",
        )
        if script_res is None:
            script_res = {}
        metadata["logs"] = script_res.get("logs", {})
        metadata["result"] = script_res.get("result", 0)
        metadata["result_message"] = script_res.get("result_message", "SUCCESS")
        return metadata

    async def __call__(self, session):
        metadata = {}

        tabs = TabSwitcher(session, self.options)
        await tabs.create_windows()
        visited = 0

        for current, url in enumerate(self.urls):
            logger.info("%d/%d %s" % (current + 1, self.max_urls, url))
            retries = 0
            while retries < 3:
                try:
                    await asyncio.wait_for(session.get(url), 5)
                    visited += 1
                    break
                except asyncio.TimeoutError:
                    retries += 1

            if current == self.max_urls - 1:
                break

            # switch to the next tab
            await tabs.switch()

        metadata["visited_url"] = visited
        await self.sync(session, metadata)
        return metadata


async def full(session, options):
    builder = Builder(options)
    return await builder(session)
