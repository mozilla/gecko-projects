/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

// The ext-* files are imported into the same scopes.
/* import-globals-from ext-devtools.js */

var {
  SpreadArgs,
} = ExtensionCommon;

this.devtools_network = class extends ExtensionAPI {
  getAPI(context) {
    return {
      devtools: {
        network: {
          onNavigated: new EventManager(context, "devtools.onNavigated", fire => {
            let listener = (event, data) => {
              fire.async(data.url);
            };

            let targetPromise = getDevToolsTargetForContext(context);
            targetPromise.then(target => {
              target.on("navigate", listener);
            });
            return () => {
              targetPromise.then(target => {
                target.off("navigate", listener);
              });
            };
          }).api(),

          getHAR: function() {
            return context.devToolsToolbox.getHARFromNetMonitor();
          },

          onRequestFinished: new EventManager(context, "devtools.network.onRequestFinished", fire => {
            const listener = (data) => {
              fire.async(data);
            };

            const toolbox = context.devToolsToolbox;
            toolbox.addRequestFinishedListener(listener);

            return () => {
              toolbox.removeRequestFinishedListener(listener);
            };
          }).api(),

          // The following method is used internally to allow the request API
          // piece that is running in the child process to ask the parent process
          // to fetch response content from the back-end.
          Request: {
            async getContent(requestId) {
              return context.devToolsToolbox.fetchResponseContent(requestId)
                .then(({content}) => new SpreadArgs([content.text, content.mimeType]))
                .catch(err => {
                  const debugName = context.extension.policy.debugName;
                  const errorMsg = "Unexpected error while fetching response content";
                  Cu.reportError(`${debugName}: ${errorMsg} for ${requestId}: ${err}`);
                  throw new ExtensionError(errorMsg);
                });
            },
          },
        },
      },
    };
  }
};
