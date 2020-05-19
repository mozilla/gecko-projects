/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env mozilla/frame-script */

const MONITOR_URL = RPMGetStringPref(
  "browser.contentblocking.report.monitor.url",
  ""
);
const MONITOR_SIGN_IN_URL = RPMGetStringPref(
  "browser.contentblocking.report.monitor.sign_in_url",
  ""
);
const HOW_IT_WORKS_URL_PREF = RPMGetFormatURLPref(
  "browser.contentblocking.report.monitor.how_it_works.url"
);
const MONITOR_PREFERENCES_URL = RPMGetFormatURLPref(
  "browser.contentblocking.report.monitor.preferences_url"
);
const MONITOR_HOME_PAGE_URL = RPMGetFormatURLPref(
  "browser.contentblocking.report.monitor.home_page_url"
);

export default class MonitorClass {
  constructor(document) {
    this.doc = document;
  }

  init() {
    // Wait for monitor data and display the card.
    this.getMonitorData();

    let monitorReportLink = this.doc.getElementById("full-report-link");
    monitorReportLink.addEventListener("click", () => {
      this.doc.sendTelemetryEvent("click", "mtr_report_link");
      RPMSendAsyncMessage("ClearMonitorCache");
    });

    let monitorAboutLink = this.doc.getElementById("monitor-link");
    monitorAboutLink.addEventListener("click", () => {
      this.doc.sendTelemetryEvent("click", "mtr_about_link");
    });

    const storedEmailLink = this.doc.querySelector(".monitor-block.email a");
    storedEmailLink.href = MONITOR_PREFERENCES_URL;

    const knownBreachesLink = this.doc.querySelector(
      ".monitor-block.breaches a"
    );
    knownBreachesLink.href = MONITOR_HOME_PAGE_URL;

    const exposedPasswordsLink = this.doc.querySelector(
      ".monitor-block.passwords a"
    );
    exposedPasswordsLink.href = MONITOR_HOME_PAGE_URL;
  }

  /**
   * Retrieves the monitor data and displays this data in the card.
   **/
  getMonitorData() {
    RPMSendQuery("FetchMonitorData", {}).then(monitorData => {
      // Once data for the user is retrieved, display the monitor card.
      this.buildContent(monitorData);

      // Show the Monitor card.
      const monitorUI = this.doc.querySelector(".card.monitor-card.loading");
      monitorUI.classList.remove("loading");
    });
  }

  buildContent(monitorData) {
    const headerContent = this.doc.querySelector(
      "#monitor-header-content span"
    );
    const monitorCard = this.doc.querySelector(".card.monitor-card");
    if (!monitorData.error) {
      monitorCard.classList.add("has-logins");
      headerContent.setAttribute(
        "data-l10n-id",
        "monitor-header-content-signed-in"
      );
      this.renderContentForUserWithAccount(monitorData);
    } else {
      monitorCard.classList.add("no-logins");
      const signUpForMonitorLink = this.doc.getElementById(
        "sign-up-for-monitor-link"
      );
      signUpForMonitorLink.href = this.buildMonitorUrl(monitorData.userEmail);
      signUpForMonitorLink.setAttribute("data-l10n-id", "monitor-sign-up");
      headerContent.setAttribute(
        "data-l10n-id",
        "monitor-header-content-no-account"
      );
      signUpForMonitorLink.addEventListener("click", () => {
        this.doc.sendTelemetryEvent("click", "mtr_signup_button");
      });
    }
  }

  /**
   * Builds the appropriate URL that takes the user to the Monitor website's
   * sign-up/sign-in page.
   *
   * @param {String|null} email
   *        Optional. The email used to direct the user to the Monitor website's OAuth
   *        sign-in flow. If null, then direct user to just the Monitor website.
   *
   * @return URL to Monitor website.
   */
  buildMonitorUrl(email = null) {
    return email
      ? `${MONITOR_SIGN_IN_URL}${encodeURIComponent(email)}`
      : MONITOR_URL;
  }

  renderContentForUserWithAccount(monitorData) {
    const monitorCardBody = this.doc.querySelector(
      ".card.monitor-card .card-body"
    );
    monitorCardBody.classList.remove("hidden");

    const monitorLinkTag = this.doc.getElementById("monitor-inline-link");
    monitorLinkTag.href = this.buildMonitorUrl(monitorData.userEmail);

    const howItWorksLink = this.doc.getElementById("monitor-link");
    howItWorksLink.href = HOW_IT_WORKS_URL_PREF;

    const storedEmail = this.doc.querySelector(
      "span[data-type='stored-emails']"
    );
    const knownBreaches = this.doc.querySelector(
      "span[data-type='known-breaches']"
    );
    const exposedPasswords = this.doc.querySelector(
      "span[data-type='exposed-passwords']"
    );

    storedEmail.textContent = monitorData.monitoredEmails;
    knownBreaches.textContent = monitorData.numBreaches;
    exposedPasswords.textContent = monitorData.passwords;

    const infoMonitoredAddresses = this.doc.getElementById(
      "info-monitored-addresses"
    );
    infoMonitoredAddresses.setAttribute(
      "data-l10n-args",
      JSON.stringify({ count: monitorData.monitoredEmails })
    );
    infoMonitoredAddresses.setAttribute(
      "data-l10n-id",
      "info-monitored-emails"
    );

    const infoKnownBreaches = this.doc.getElementById("info-known-breaches");
    infoKnownBreaches.setAttribute(
      "data-l10n-args",
      JSON.stringify({ count: monitorData.numBreaches })
    );
    infoKnownBreaches.setAttribute("data-l10n-id", "info-known-breaches-found");

    const infoExposedPasswords = this.doc.getElementById(
      "info-exposed-passwords"
    );
    infoExposedPasswords.setAttribute(
      "data-l10n-args",
      JSON.stringify({ count: monitorData.passwords })
    );
    infoExposedPasswords.setAttribute(
      "data-l10n-id",
      "info-exposed-passwords-found"
    );
  }
}
