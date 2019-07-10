/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check evaluating and expanding getters in the console.
const TEST_URI =
  "data:text/html;charset=utf8,<h1>Object Inspector on Getters</h1>";
const { ELLIPSIS } = require("devtools/shared/l10n");

add_task(async function() {
  const hud = await openNewTabAndConsole(TEST_URI);

  const LONGSTRING = "ab ".repeat(1e5);

  await ContentTask.spawn(gBrowser.selectedBrowser, LONGSTRING, function(
    longString
  ) {
    content.wrappedJSObject.console.log(
      "oi-test",
      Object.create(
        null,
        Object.getOwnPropertyDescriptors({
          get myStringGetter() {
            return "hello";
          },
          get myNumberGetter() {
            return 123;
          },
          get myUndefinedGetter() {
            return undefined;
          },
          get myNullGetter() {
            return null;
          },
          get myObjectGetter() {
            return { foo: "bar" };
          },
          get myArrayGetter() {
            return Array.from({ length: 1000 }, (_, i) => i);
          },
          get myMapGetter() {
            return new Map([["foo", { bar: "baz" }]]);
          },
          get myProxyGetter() {
            const handler = {
              get: function(target, name) {
                return name in target ? target[name] : 37;
              },
            };
            return new Proxy({ a: 1 }, handler);
          },
          get myThrowingGetter() {
            throw new Error("myError");
          },
          get myLongStringGetter() {
            return longString;
          },
        })
      )
    );
  });

  const node = await waitFor(() => findMessage(hud, "oi-test"));
  const oi = node.querySelector(".tree");

  expandObjectInspectorNode(oi);
  await waitFor(() => getObjectInspectorNodes(oi).length > 1);

  await testStringGetter(oi);
  await testNumberGetter(oi);
  await testUndefinedGetter(oi);
  await testNullGetter(oi);
  await testObjectGetter(oi);
  await testArrayGetter(oi);
  await testMapGetter(oi);
  await testProxyGetter(oi);
  await testThrowingGetter(oi);
  await testLongStringGetter(oi, LONGSTRING);
});

async function testStringGetter(oi) {
  let node = findObjectInspectorNode(oi, "myStringGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myStringGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myStringGetter");
  ok(
    node.textContent.includes(`myStringGetter: "hello"`),
    "String getter now has the expected text content"
  );
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
}

async function testNumberGetter(oi) {
  let node = findObjectInspectorNode(oi, "myNumberGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myNumberGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myNumberGetter");
  ok(
    node.textContent.includes(`myNumberGetter: 123`),
    "Number getter now has the expected text content"
  );
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
}

async function testUndefinedGetter(oi) {
  let node = findObjectInspectorNode(oi, "myUndefinedGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myUndefinedGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myUndefinedGetter");
  ok(
    node.textContent.includes(`myUndefinedGetter: undefined`),
    "undefined getter now has the expected text content"
  );
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
}

async function testNullGetter(oi) {
  let node = findObjectInspectorNode(oi, "myNullGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myNullGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myNullGetter");
  ok(
    node.textContent.includes(`myNullGetter: null`),
    "null getter now has the expected text content"
  );
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
}

async function testObjectGetter(oi) {
  let node = findObjectInspectorNode(oi, "myObjectGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myObjectGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myObjectGetter");
  ok(
    node.textContent.includes(`myObjectGetter: Object { foo: "bar" }`),
    "object getter now has the expected text content"
  );
  is(isObjectInspectorNodeExpandable(node), true, "The node can be expanded");

  expandObjectInspectorNode(node);
  await waitFor(() => getObjectInspectorChildrenNodes(node).length > 0);
  checkChildren(node, [`foo: "bar"`, `<prototype>`]);
}

async function testArrayGetter(oi) {
  let node = findObjectInspectorNode(oi, "myArrayGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myArrayGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myArrayGetter");
  ok(
    node.textContent.includes(
      `myArrayGetter: Array(1000) [ 0, 1, 2, ${ELLIPSIS} ]`
    ),
    "Array getter now has the expected text content - "
  );
  is(isObjectInspectorNodeExpandable(node), true, "The node can be expanded");

  expandObjectInspectorNode(node);
  await waitFor(() => getObjectInspectorChildrenNodes(node).length > 0);
  const children = getObjectInspectorChildrenNodes(node);

  const firstBucket = children[0];
  ok(firstBucket.textContent.includes(`[0${ELLIPSIS}99]`), "Array has buckets");

  is(
    isObjectInspectorNodeExpandable(firstBucket),
    true,
    "The bucket can be expanded"
  );
  expandObjectInspectorNode(firstBucket);
  await waitFor(() => getObjectInspectorChildrenNodes(firstBucket).length > 0);
  checkChildren(
    firstBucket,
    Array.from({ length: 100 }, (_, i) => `${i}: ${i}`)
  );
}

async function testMapGetter(oi) {
  let node = findObjectInspectorNode(oi, "myMapGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myMapGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myMapGetter");
  ok(
    node.textContent.includes(`myMapGetter: Map`),
    "map getter now has the expected text content"
  );
  is(isObjectInspectorNodeExpandable(node), true, "The node can be expanded");

  expandObjectInspectorNode(node);
  await waitFor(() => getObjectInspectorChildrenNodes(node).length > 0);
  checkChildren(node, [`size`, `<entries>`, `<prototype>`]);

  const entriesNode = findObjectInspectorNode(oi, "<entries>");
  expandObjectInspectorNode(entriesNode);
  await waitFor(() => getObjectInspectorChildrenNodes(entriesNode).length > 0);
  checkChildren(entriesNode, [`foo → Object { ${ELLIPSIS} }`]);

  const entryNode = getObjectInspectorChildrenNodes(entriesNode)[0];
  expandObjectInspectorNode(entryNode);
  await waitFor(() => getObjectInspectorChildrenNodes(entryNode).length > 0);
  checkChildren(entryNode, [`<key>: "foo"`, `<value>: Object { ${ELLIPSIS} }`]);
}

async function testProxyGetter(oi) {
  let node = findObjectInspectorNode(oi, "myProxyGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myProxyGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myProxyGetter");
  ok(
    node.textContent.includes(`myProxyGetter: Proxy`),
    "proxy getter now has the expected text content"
  );
  is(isObjectInspectorNodeExpandable(node), true, "The node can be expanded");

  expandObjectInspectorNode(node);
  await waitFor(() => getObjectInspectorChildrenNodes(node).length > 0);
  checkChildren(node, [`<target>`, `<handler>`]);

  const targetNode = findObjectInspectorNode(oi, "<target>");
  expandObjectInspectorNode(targetNode);
  await waitFor(() => getObjectInspectorChildrenNodes(targetNode).length > 0);
  checkChildren(targetNode, [`a: 1`, `<prototype>`]);

  const handlerNode = findObjectInspectorNode(oi, "<handler>");
  expandObjectInspectorNode(handlerNode);
  await waitFor(() => getObjectInspectorChildrenNodes(handlerNode).length > 0);
  checkChildren(handlerNode, [`get:`, `<prototype>`]);
}

async function testThrowingGetter(oi) {
  let node = findObjectInspectorNode(oi, "myThrowingGetter");
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(
    () =>
      !getObjectInspectorInvokeGetterButton(
        findObjectInspectorNode(oi, "myThrowingGetter")
      )
  );

  node = findObjectInspectorNode(oi, "myThrowingGetter");
  ok(
    node.textContent.includes(`myThrowingGetter: Error: "myError"`),
    "throwing getter does show the error"
  );
  is(isObjectInspectorNodeExpandable(node), true, "The node can be expanded");

  expandObjectInspectorNode(node);
  await waitFor(() => getObjectInspectorChildrenNodes(node).length > 0);
  checkChildren(node, [
    `columnNumber`,
    `fileName`,
    `lineNumber`,
    `message`,
    `stack`,
    `<prototype>`,
  ]);
}

async function testLongStringGetter(oi, longString) {
  const getLongStringNode = () =>
    findObjectInspectorNode(oi, "myLongStringGetter");
  const node = getLongStringNode();
  is(
    isObjectInspectorNodeExpandable(node),
    false,
    "The node can't be expanded"
  );
  const invokeButton = getObjectInspectorInvokeGetterButton(node);
  ok(invokeButton, "There is an invoke button as expected");

  invokeButton.click();
  await waitFor(() =>
    getLongStringNode().textContent.includes(`myLongStringGetter: "ab ab`)
  );
  ok(true, "longstring getter shows the initial text");
  is(
    isObjectInspectorNodeExpandable(getLongStringNode()),
    true,
    "The node can be expanded"
  );

  expandObjectInspectorNode(getLongStringNode());
  await waitFor(() =>
    getLongStringNode().textContent.includes(
      `myLongStringGetter: "${longString}"`
    )
  );
  ok(true, "the longstring was expanded");
}

function checkChildren(node, expectedChildren) {
  const children = getObjectInspectorChildrenNodes(node);
  is(
    children.length,
    expectedChildren.length,
    "There is the expected number of children"
  );
  children.forEach((child, index) => {
    ok(
      child.textContent.includes(expectedChildren[index]),
      `Expected "${expectedChildren[index]}" child`
    );
  });
}
