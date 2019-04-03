/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// @flow

import assert from "../../utils/assert";
import { recordEvent } from "../../utils/telemetry";
import { remapBreakpoints } from "../breakpoints";

import { setSymbols } from "./symbols";
import { prettyPrint } from "../../workers/pretty-print";
import { getPrettySourceURL, isLoaded } from "../../utils/source";
import { loadSourceText } from "./loadSourceText";
import { mapFrames } from "../pause";
import { selectSpecificLocation } from "../sources";

import {
  getSource,
  getSourceFromId,
  getSourceThreads,
  getSourceByURL,
  getSelectedLocation
} from "../../selectors";

import type { Action, ThunkArgs } from "../types";
import { selectSource } from "./select";
import type { JsSource, Source } from "../../types";

export async function prettyPrintSource(
  sourceMaps: any,
  prettySource: Source,
  generatedSource: any
) {
  const url = getPrettySourceURL(generatedSource.url);
  const { code, mappings } = await prettyPrint({
    source: generatedSource,
    url: url
  });
  await sourceMaps.applySourceMap(generatedSource.id, url, code, mappings);

  // The source map URL service used by other devtools listens to changes to
  // sources based on their actor IDs, so apply the mapping there too.
  for (const sourceActor of generatedSource.actors) {
    await sourceMaps.applySourceMap(sourceActor.actor, url, code, mappings);
  }
  return {
    id: prettySource.id,
    text: code,
    contentType: "text/javascript"
  };
}

export function createPrettySource(sourceId: string) {
  return async ({ dispatch, getState, sourceMaps }: ThunkArgs) => {
    const source = getSourceFromId(getState(), sourceId);
    const url = getPrettySourceURL(source.url);
    const id = await sourceMaps.generatedToOriginalId(sourceId, url);

    const prettySource: JsSource = {
      url,
      relativeUrl: url,
      id,
      isBlackBoxed: false,
      isPrettyPrinted: true,
      isWasm: false,
      contentType: "text/javascript",
      loadedState: "loading",
      introductionUrl: null,
      introductionType: undefined,
      isExtension: false,
      actors: []
    };

    dispatch(({ type: "ADD_SOURCE", source: prettySource }: Action));
    await dispatch(selectSource(prettySource.id));

    return prettySource;
  };
}

function selectPrettyLocation(prettySource: Source) {
  return async ({ dispatch, sourceMaps, getState }: ThunkArgs) => {
    let location = getSelectedLocation(getState());

    if (location) {
      location = await sourceMaps.getOriginalLocation(location);
      return dispatch(
        selectSpecificLocation({ ...location, sourceId: prettySource.id })
      );
    }

    return dispatch(selectSource(prettySource.id));
  };
}

/**
 * Toggle the pretty printing of a source's text. All subsequent calls to
 * |getText| will return the pretty-toggled text. Nothing will happen for
 * non-javascript files.
 *
 * @memberof actions/sources
 * @static
 * @param string id The source form from the RDP.
 * @returns Promise
 *          A promise that resolves to [aSource, prettyText] or rejects to
 *          [aSource, error].
 */
export function togglePrettyPrint(sourceId: string) {
  return async ({ dispatch, getState, client, sourceMaps }: ThunkArgs) => {
    const source = getSource(getState(), sourceId);
    if (!source) {
      return {};
    }

    if (!source.isPrettyPrinted) {
      recordEvent("pretty_print");
    }

    if (!isLoaded(source)) {
      await dispatch(loadSourceText({ source }));
    }

    assert(
      sourceMaps.isGeneratedId(sourceId),
      "Pretty-printing only allowed on generated sources"
    );

    const url = getPrettySourceURL(source.url);
    const prettySource = getSourceByURL(getState(), url);

    if (prettySource) {
      return dispatch(selectPrettyLocation(prettySource));
    }

    const newPrettySource = await dispatch(createPrettySource(sourceId));
    await dispatch(selectPrettyLocation(newPrettySource));

    await dispatch(remapBreakpoints(sourceId));

    const threads = getSourceThreads(getState(), source);
    await Promise.all(threads.map(thread => dispatch(mapFrames(thread))));

    await dispatch(setSymbols({ source: newPrettySource }));

    return newPrettySource;
  };
}
