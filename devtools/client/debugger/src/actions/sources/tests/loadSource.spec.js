/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// @flow

import {
  actions,
  selectors,
  watchForState,
  createStore,
  makeOriginalSource,
  makeSource
} from "../../../utils/test-head";
import {
  createSource,
  sourceThreadClient
} from "../../tests/helpers/threadClient.js";
import { getBreakpointsList } from "../../../selectors";

describe("loadSourceText", () => {
  it("should load source text", async () => {
    const store = createStore(sourceThreadClient);
    const { dispatch, getState, cx } = store;

    const foo1Source = await dispatch(
      actions.newGeneratedSource(makeSource("foo1"))
    );
    await dispatch(actions.loadSourceText({ cx, source: foo1Source }));
    const fooSource = selectors.getSource(getState(), "foo1");

    if (!fooSource || typeof fooSource.text != "string") {
      throw new Error("bad fooSource");
    }
    expect(fooSource.text.indexOf("return foo1")).not.toBe(-1);

    const baseFoo2Source = await dispatch(
      actions.newGeneratedSource(makeSource("foo2"))
    );
    await dispatch(actions.loadSourceText({ cx, source: baseFoo2Source }));
    const foo2Source = selectors.getSource(getState(), "foo2");

    if (!foo2Source || typeof foo2Source.text != "string") {
      throw new Error("bad fooSource");
    }
    expect(foo2Source.text.indexOf("return foo2")).not.toBe(-1);
  });

  it("should update breakpoint text when a source loads", async () => {
    const fooOrigContent = createSource("fooOrig", "var fooOrig = 42;");
    const fooGenContent = createSource("fooGen", "var fooGen = 42;");

    const store = createStore(
      {
        ...sourceThreadClient,
        sourceContents: async () => fooGenContent,
        getBreakpointPositions: async () => ({ "1": [0] })
      },
      {},
      {
        getGeneratedRangesForOriginal: async () => [
          { start: { line: 1, column: 0 }, end: { line: 1, column: 1 } }
        ],
        getOriginalLocations: async items =>
          items.map(item => ({
            ...item,
            sourceId: fooOrigSource.id
          })),
        getOriginalSourceText: async s => ({
          text: fooOrigContent.source,
          contentType: fooOrigContent.contentType
        })
      }
    );
    const { cx, dispatch, getState } = store;

    const fooGenSource = await dispatch(
      actions.newGeneratedSource(makeSource("fooGen"))
    );
    const fooOrigSource = await dispatch(
      actions.newOriginalSource(makeOriginalSource(fooGenSource))
    );

    const location = {
      sourceId: fooOrigSource.id,
      line: 1,
      column: 0
    };
    await dispatch(actions.addBreakpoint(cx, location, {}));

    const breakpoint = getBreakpointsList(getState())[0];

    expect(breakpoint.text).toBe("");
    expect(breakpoint.originalText).toBe("");

    await dispatch(actions.loadSourceText({ cx, source: fooOrigSource }));

    const breakpoint1 = getBreakpointsList(getState())[0];
    expect(breakpoint1.text).toBe("");
    expect(breakpoint1.originalText).toBe("var fooOrig = 42;");

    await dispatch(actions.loadSourceText({ cx, source: fooGenSource }));

    const breakpoint2 = getBreakpointsList(getState())[0];
    expect(breakpoint2.text).toBe("var fooGen = 42;");
    expect(breakpoint2.originalText).toBe("var fooOrig = 42;");
  });

  it("loads two sources w/ one request", async () => {
    let resolve;
    let count = 0;
    const { dispatch, getState, cx } = createStore({
      sourceContents: () =>
        new Promise(r => {
          count++;
          resolve = r;
        }),
      getBreakpointPositions: async () => ({})
    });
    const id = "foo";

    await dispatch(
      actions.newGeneratedSource(makeSource(id, { loadedState: "unloaded" }))
    );

    let source = selectors.getSourceFromId(getState(), id);
    dispatch(actions.loadSourceText({ cx, source }));

    source = selectors.getSourceFromId(getState(), id);
    const loading = dispatch(actions.loadSourceText({ cx, source }));

    if (!resolve) {
      throw new Error("no resolve");
    }
    resolve({ source: "yay", contentType: "text/javascript" });
    await loading;
    expect(count).toEqual(1);

    source = selectors.getSource(getState(), id);
    expect(source && source.text).toEqual("yay");
  });

  it("doesn't re-load loaded sources", async () => {
    let resolve;
    let count = 0;
    const { dispatch, getState, cx } = createStore({
      sourceContents: () =>
        new Promise(r => {
          count++;
          resolve = r;
        }),
      getBreakpointPositions: async () => ({})
    });
    const id = "foo";

    await dispatch(
      actions.newGeneratedSource(makeSource(id, { loadedState: "unloaded" }))
    );
    let source = selectors.getSourceFromId(getState(), id);
    const loading = dispatch(actions.loadSourceText({ cx, source }));

    if (!resolve) {
      throw new Error("no resolve");
    }
    resolve({ source: "yay", contentType: "text/javascript" });
    await loading;

    source = selectors.getSourceFromId(getState(), id);
    await dispatch(actions.loadSourceText({ cx, source }));
    expect(count).toEqual(1);

    source = selectors.getSource(getState(), id);
    expect(source && source.text).toEqual("yay");
  });

  it("should cache subsequent source text loads", async () => {
    const { dispatch, getState, cx } = createStore(sourceThreadClient);

    const source = await dispatch(
      actions.newGeneratedSource(makeSource("foo1"))
    );
    await dispatch(actions.loadSourceText({ cx, source }));
    const prevSource = selectors.getSourceFromId(getState(), "foo1");

    await dispatch(actions.loadSourceText({ cx, source: prevSource }));
    const curSource = selectors.getSource(getState(), "foo1");

    expect(prevSource === curSource).toBeTruthy();
  });

  it("should indicate a loading source", async () => {
    const store = createStore(sourceThreadClient);
    const { dispatch, cx } = store;

    const source = await dispatch(
      actions.newGeneratedSource(makeSource("foo2"))
    );

    const wasLoading = watchForState(store, state => {
      const fooSource = selectors.getSource(state, "foo2");
      return fooSource && fooSource.loadedState === "loading";
    });

    await dispatch(actions.loadSourceText({ cx, source }));

    expect(wasLoading()).toBe(true);
  });

  it("should indicate an errored source text", async () => {
    const { dispatch, getState, cx } = createStore(sourceThreadClient);

    const source = await dispatch(
      actions.newGeneratedSource(makeSource("bad-id"))
    );
    await dispatch(actions.loadSourceText({ cx, source }));
    const badSource = selectors.getSource(getState(), "bad-id");

    if (!badSource || !badSource.error) {
      throw new Error("bad badSource");
    }
    expect(badSource.error.indexOf("unknown source")).not.toBe(-1);
  });
});
