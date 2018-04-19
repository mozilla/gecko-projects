/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { createFactory, PureComponent } = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");

const AnimatedPropertyList = createFactory(require("./AnimatedPropertyList"));
const AnimatedPropertyListHeader = createFactory(require("./AnimatedPropertyListHeader"));

class AnimatedPropertyListContainer extends PureComponent {
  static get propTypes() {
    return {
      animation: PropTypes.object.isRequired,
      emitEventForTest: PropTypes.func.isRequired,
      getAnimatedPropertyMap: PropTypes.func.isRequired,
      getComputedStyle: PropTypes.func.isRequired,
      simulateAnimation: PropTypes.func.isRequired,
    };
  }

  render() {
    const {
      animation,
      emitEventForTest,
      getAnimatedPropertyMap,
      getComputedStyle,
      simulateAnimation,
    } = this.props;

    return dom.div(
      {
        className: `animated-property-list-container ${ animation.state.type }`
      },
      AnimatedPropertyListHeader(),
      AnimatedPropertyList(
        {
          animation,
          emitEventForTest,
          getAnimatedPropertyMap,
          getComputedStyle,
          simulateAnimation,
        }
      )
    );
  }
}

module.exports = AnimatedPropertyListContainer;
