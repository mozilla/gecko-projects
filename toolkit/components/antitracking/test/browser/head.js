/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const TEST_DOMAIN = "http://example.net/";
const TEST_DOMAIN_2 = "http://xn--exmple-cua.test/";
const TEST_DOMAIN_3 = "https://xn--hxajbheg2az3al.xn--jxalpdlp/";
const TEST_DOMAIN_4 = "http://prefixexample.com/";
const TEST_DOMAIN_5 = "http://test/";
const TEST_DOMAIN_6 = "http://mochi.test:8888/";
const TEST_3RD_PARTY_DOMAIN = "https://tracking.example.org/";
const TEST_3RD_PARTY_DOMAIN_TP = "https://tracking.example.com/";
const TEST_4TH_PARTY_DOMAIN = "http://not-tracking.example.com/";
const TEST_ANOTHER_3RD_PARTY_DOMAIN = "https://another-tracking.example.net/";

const TEST_PATH = "browser/toolkit/components/antitracking/test/browser/";

const TEST_TOP_PAGE = TEST_DOMAIN + TEST_PATH + "page.html";
const TEST_TOP_PAGE_2 = TEST_DOMAIN_2 + TEST_PATH + "page.html";
const TEST_TOP_PAGE_3 = TEST_DOMAIN_3 + TEST_PATH + "page.html";
const TEST_TOP_PAGE_4 = TEST_DOMAIN_4 + TEST_PATH + "page.html";
const TEST_TOP_PAGE_5 = TEST_DOMAIN_5 + TEST_PATH + "page.html";
const TEST_TOP_PAGE_6 = TEST_DOMAIN_6 + TEST_PATH + "page.html";
const TEST_EMBEDDER_PAGE = TEST_DOMAIN + TEST_PATH + "embedder.html";
const TEST_POPUP_PAGE = TEST_DOMAIN + TEST_PATH + "popup.html";
const TEST_3RD_PARTY_PAGE = TEST_3RD_PARTY_DOMAIN + TEST_PATH + "3rdParty.html";
const TEST_3RD_PARTY_PAGE_WO = TEST_3RD_PARTY_DOMAIN + TEST_PATH + "3rdPartyWO.html";
const TEST_3RD_PARTY_PAGE_UI = TEST_3RD_PARTY_DOMAIN + TEST_PATH + "3rdPartyUI.html";
const TEST_3RD_PARTY_PAGE_WITH_SVG = TEST_3RD_PARTY_DOMAIN + TEST_PATH + "3rdPartySVG.html";
const TEST_4TH_PARTY_PAGE = TEST_4TH_PARTY_DOMAIN + TEST_PATH + "3rdParty.html";
const TEST_ANOTHER_3RD_PARTY_PAGE = TEST_ANOTHER_3RD_PARTY_DOMAIN + TEST_PATH + "3rdParty.html";
const TEST_3RD_PARTY_STORAGE_PAGE = TEST_3RD_PARTY_DOMAIN + TEST_PATH + "3rdPartyStorage.html";

const BEHAVIOR_ACCEPT         = Ci.nsICookieService.BEHAVIOR_ACCEPT;
const BEHAVIOR_REJECT         = Ci.nsICookieService.BEHAVIOR_REJECT;
const BEHAVIOR_LIMIT_FOREIGN  = Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN;
const BEHAVIOR_REJECT_FOREIGN = Ci.nsICookieService.BEHAVIOR_REJECT_FOREIGN;
const BEHAVIOR_REJECT_TRACKER = Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER;

requestLongerTimeout(3);

const {UrlClassifierTestUtils} = ChromeUtils.import("resource://testing-common/UrlClassifierTestUtils.jsm");

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/antitracking/test/browser/antitracking_head.js",
  this);

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/toolkit/components/antitracking/test/browser/storageprincipal_head.js",
  this);
