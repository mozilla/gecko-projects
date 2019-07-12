/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { RichText } from "../../components/RichText/RichText";
import { safeURI } from "../../template-utils";
import { SnippetBase } from "../../components/SnippetBase/SnippetBase";

const DEFAULT_ICON_PATH = "chrome://branding/content/icon64.png";
// Alt text placeholder in case the prop from the server isn't available
const ICON_ALT_TEXT = "";

export class SimpleBelowSearchSnippet extends React.PureComponent {
  renderText() {
    const { props } = this;
    return (
      <RichText
        text={props.content.text}
        customElements={this.props.customElements}
        localization_id="text"
        links={props.content.links}
        sendClick={props.sendClick}
      />
    );
  }

  render() {
    const { props } = this;
    let className = "SimpleBelowSearchSnippet";

    if (props.className) {
      className += ` ${props.className}`;
    }

    return (
      <SnippetBase
        {...props}
        className={className}
        textStyle={this.props.textStyle}
      >
        <img
          src={safeURI(props.content.icon) || DEFAULT_ICON_PATH}
          className="icon icon-light-theme"
          alt={props.content.icon_alt_text || ICON_ALT_TEXT}
        />
        <img
          src={
            safeURI(props.content.icon_dark_theme || props.content.icon) ||
            DEFAULT_ICON_PATH
          }
          className="icon icon-dark-theme"
          alt={props.content.icon_alt_text || ICON_ALT_TEXT}
        />
        <div>
          <p className="body">{this.renderText()}</p>
          {this.props.extraContent}
        </div>
      </SnippetBase>
    );
  }
}
