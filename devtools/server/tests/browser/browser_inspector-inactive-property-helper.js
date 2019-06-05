/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test expected outputs of the inactivePropertyHelper's isPropertyUsed function.

// This is more of a unit test than a mochitest-browser test, but can't be tested with an
// xpcshell test as the inactivePropertyHelper requires the DOM to work (e.g for computed
// styles).

add_task(async function() {
  await pushPref("devtools.inspector.inactive.css.enabled", true);

  const {inactivePropertyHelper} = require("devtools/server/actors/utils/inactive-property-helper");
  let { isPropertyUsed } = inactivePropertyHelper;
  isPropertyUsed = isPropertyUsed.bind(inactivePropertyHelper);

  // A single test case is an object of the following shape:
  // - {String} info: a summary of the test case
  // - {String} property: the CSS property that should be tested
  // - {String} tagName: the tagName of the element we're going to test
  // - {Array<String>} rules: An array of the rules that will be applied on the element.
  //                          This can't be empty as isPropertyUsed need a rule.
  // - {Integer} ruleIndex (optional): If there are multiples rules in `rules`, the index
  //                                   of the one that should be tested in isPropertyUsed.
  // - {Boolean} isActive: should the property be active (isPropertyUsed `used` result).
  const tests = [{
    info: "vertical-align is inactive on a block element",
    property: "vertical-align",
    tagName: "div",
    rules: ["div { vertical-align: top; }"],
    isActive: false,
  }, {
    info: "vertical-align is inactive on a span with display block",
    property: "vertical-align",
    tagName: "span",
    rules: ["span { vertical-align: top; display: block;}"],
    isActive: false,
  }, {
    info: "vertical-align is active on a div with display inline-block",
    property: "vertical-align",
    tagName: "div",
    rules: ["div { vertical-align: top; display: inline-block;}"],
    isActive: true,
  }, {
    info: "vertical-align is active on a table-cell",
    property: "vertical-align",
    tagName: "div",
    rules: ["div { vertical-align: top; display: table-cell;}"],
    isActive: true,
  }, {
    info: "vertical-align is active on a block element ::first-letter",
    property: "vertical-align",
    tagName: "div",
    rules: ["div::first-letter { vertical-align: top; }"],
    isActive: true,
  }, {
    info: "vertical-align is active on a block element ::first-line",
    property: "vertical-align",
    tagName: "div",
    rules: ["div::first-line { vertical-align: top; }"],
    isActive: true,
  }, {
    info: "vertical-align is active on an inline element",
    property: "vertical-align",
    tagName: "span",
    rules: ["span { vertical-align: top; }"],
    isActive: true,
  }, {
    info: "width is inactive on a non-replaced inline element",
    property: "width",
    tagName: "span",
    rules: ["span { width: 500px; }"],
    isActive: false,
  }, {
    info: "min-width is inactive on a non-replaced inline element",
    property: "min-width",
    tagName: "span",
    rules: ["span { min-width: 500px; }"],
    isActive: false,
  }, {
    info: "max-width is inactive on a non-replaced inline element",
    property: "max-width",
    tagName: "span",
    rules: ["span { max-width: 500px; }"],
    isActive: false,
  }, {
    info: "width is inactive on an tr element",
    property: "width",
    tagName: "tr",
    rules: ["tr { width: 500px; }"],
    isActive: false,
  }, {
    info: "min-width is inactive on an tr element",
    property: "min-width",
    tagName: "tr",
    rules: ["tr { min-width: 500px; }"],
    isActive: false,
  }, {
    info: "max-width is inactive on an tr element",
    property: "max-width",
    tagName: "tr",
    rules: ["tr { max-width: 500px; }"],
    isActive: false,
  }, {
    info: "width is inactive on an thead element",
    property: "width",
    tagName: "thead",
    rules: ["thead { width: 500px; }"],
    isActive: false,
  }, {
    info: "min-width is inactive on an thead element",
    property: "min-width",
    tagName: "thead",
    rules: ["thead { min-width: 500px; }"],
    isActive: false,
  }, {
    info: "max-width is inactive on an thead element",
    property: "max-width",
    tagName: "thead",
    rules: ["thead { max-width: 500px; }"],
    isActive: false,
  }, {
    info: "width is inactive on an tfoot element",
    property: "width",
    tagName: "tfoot",
    rules: ["tfoot { width: 500px; }"],
    isActive: false,
  }, {
    info: "min-width is inactive on an tfoot element",
    property: "min-width",
    tagName: "tfoot",
    rules: ["tfoot { min-width: 500px; }"],
    isActive: false,
  }, {
    info: "max-width is inactive on an tfoot element",
    property: "max-width",
    tagName: "tfoot",
    rules: ["tfoot { max-width: 500px; }"],
    isActive: false,
  }, {
    info: "width is active on a replaced inline element",
    property: "width",
    tagName: "img",
    rules: ["img { width: 500px; }"],
    isActive: true,
  }, {
    info: "min-width is active on a replaced inline element",
    property: "min-width",
    tagName: "img",
    rules: ["img { min-width: 500px; }"],
    isActive: true,
  }, {
    info: "max-width is active on a replaced inline element",
    property: "max-width",
    tagName: "img",
    rules: ["img { max-width: 500px; }"],
    isActive: true,
  }, {
    info: "width is active on a block element",
    property: "width",
    tagName: "div",
    rules: ["div { width: 500px; }"],
    isActive: true,
  }, {
    info: "min-width is active on a block element",
    property: "min-width",
    tagName: "div",
    rules: ["div { min-width: 500px; }"],
    isActive: true,
  }, {
    info: "max-width is active on a block element",
    property: "max-width",
    tagName: "div",
    rules: ["div { max-width: 500px; }"],
    isActive: true,
  }, {
    info: "height is inactive on a non-replaced inline element",
    property: "height",
    tagName: "span",
    rules: ["span { height: 500px; }"],
    isActive: false,
  }, {
    info: "min-height is inactive on a non-replaced inline element",
    property: "min-height",
    tagName: "span",
    rules: ["span { min-height: 500px; }"],
    isActive: false,
  }, {
    info: "max-height is inactive on a non-replaced inline element",
    property: "max-height",
    tagName: "span",
    rules: ["span { max-height: 500px; }"],
    isActive: false,
  }, {
    info: "height is inactive on colgroup element",
    property: "height",
    tagName: "colgroup",
    rules: ["colgroup { height: 500px; }"],
    isActive: false,
  }, {
    info: "min-height is inactive on colgroup element",
    property: "min-height",
    tagName: "colgroup",
    rules: ["colgroup { min-height: 500px; }"],
    isActive: false,
  }, {
    info: "max-height is inactive on colgroup element",
    property: "max-height",
    tagName: "colgroup",
    rules: ["colgroup { max-height: 500px; }"],
    isActive: false,
  }, {
    info: "height is inactive on col element",
    property: "height",
    tagName: "col",
    rules: ["col { height: 500px; }"],
    isActive: false,
  }, {
    info: "min-height is inactive on col element",
    property: "min-height",
    tagName: "col",
    rules: ["col { min-height: 500px; }"],
    isActive: false,
  }, {
    info: "max-height is inactive on col element",
    property: "max-height",
    tagName: "col",
    rules: ["col { max-height: 500px; }"],
    isActive: false,
  }, {
    info: "height is active on a replaced inline element",
    property: "height",
    tagName: "img",
    rules: ["img { height: 500px; }"],
    isActive: true,
  }, {
    info: "min-height is active on a replaced inline element",
    property: "min-height",
    tagName: "img",
    rules: ["img { min-height: 500px; }"],
    isActive: true,
  }, {
    info: "max-height is active on a replaced inline element",
    property: "max-height",
    tagName: "img",
    rules: ["img { max-height: 500px; }"],
    isActive: true,
  }, {
    info: "height is active on a block element",
    property: "height",
    tagName: "div",
    rules: ["div { height: 500px; }"],
    isActive: true,
  }, {
    info: "min-height is active on a block element",
    property: "min-height",
    tagName: "div",
    rules: ["div { min-height: 500px; }"],
    isActive: true,
  }, {
    info: "max-height is active on a block element",
    property: "max-height",
    tagName: "div",
    rules: ["div { max-height: 500px; }"],
    isActive: true,
  }];

  for (const {info: summary, property, tagName, rules, ruleIndex, isActive} of tests) {
    info(summary);

    // Create an element which will contain the test elements.
    const main = document.createElement("main");
    document.firstElementChild.appendChild(main);

    // Create the element and insert the rules
    const el = document.createElement(tagName);
    const style = document.createElement("style");
    main.append(style, el);

    for (const dataRule of rules) {
      style.sheet.insertRule(dataRule);
    }
    const rule = style.sheet.cssRules[ruleIndex || 0 ];

    const {used} = isPropertyUsed(el, getComputedStyle(el), rule, property);
    ok(used === isActive, `"${property}" is ${isActive ? "active" : "inactive"}`);

    main.remove();
  }
});
