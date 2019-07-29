/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Services } = ChromeUtils.import("resource://gre/modules/Services.jsm");
ChromeUtils.defineModuleGetter(
  this,
  "PlacesUtils",
  "resource://gre/modules/PlacesUtils.jsm"
);
ChromeUtils.defineModuleGetter(
  this,
  "NetUtil",
  "resource://gre/modules/NetUtil.jsm"
);

function makeDefaultFaviconChannel(uri, loadInfo) {
  let channel = Services.io.newChannelFromURIWithLoadInfo(
    PlacesUtils.favicons.defaultFavicon,
    loadInfo
  );
  channel.originalURI = uri;
  channel.contentType = PlacesUtils.favicons.defaultFaviconMimeType;
  return channel;
}

function streamDefaultFavicon(uri, loadInfo, outputStream, originalChannel) {
  try {
    // Open up a new channel to get that data, and push it to our output stream.
    // Create a listener to hand data to the pipe's output stream.
    let listener = Cc[
      "@mozilla.org/network/simple-stream-listener;1"
    ].createInstance(Ci.nsISimpleStreamListener);
    listener.init(outputStream, {
      onStartRequest(request) {},
      onStopRequest(request, statusCode) {
        // We must close the outputStream regardless.
        outputStream.close();
      },
    });
    originalChannel.contentType = PlacesUtils.favicons.defaultFaviconMimeType;
    let defaultIconChannel = makeDefaultFaviconChannel(uri, loadInfo);
    defaultIconChannel.asyncOpen(listener);
  } catch (ex) {
    Cu.reportError(ex);
    outputStream.close();
  }
}

function serveIcon(pipe, data) {
  // Pass the icon data to the output stream.
  let stream = Cc["@mozilla.org/binaryoutputstream;1"].createInstance(
    Ci.nsIBinaryOutputStream
  );
  stream.setOutputStream(pipe.outputStream);
  stream.writeByteArray(data);
  stream.close();
  pipe.outputStream.close();
}

function PageIconProtocolHandler() {}

PageIconProtocolHandler.prototype = {
  get scheme() {
    return "page-icon";
  },

  get defaultPort() {
    return -1;
  },

  get protocolFlags() {
    return (
      Ci.nsIProtocolHandler.URI_NORELATIVE |
      Ci.nsIProtocolHandler.URI_NOAUTH |
      Ci.nsIProtocolHandler.URI_DANGEROUS_TO_LOAD |
      Ci.nsIProtocolHandler.URI_IS_LOCAL_RESOURCE
    );
  },

  newChannel(uri, loadInfo) {
    try {
      // Create a pipe that will give us an output stream that we can use once
      // we got all the favicon data.
      let pipe = Cc["@mozilla.org/pipe;1"].createInstance(Ci.nsIPipe);
      pipe.init(true, true, 0, Ci.nsIFaviconService.MAX_FAVICON_BUFFER_SIZE);

      // Create our channel.
      let channel = Cc[
        "@mozilla.org/network/input-stream-channel;1"
      ].createInstance(Ci.nsIInputStreamChannel);
      channel.QueryInterface(Ci.nsIChannel);
      channel.setURI(uri);
      channel.contentStream = pipe.inputStream;
      channel.loadInfo = loadInfo;

      let pageURI = NetUtil.newURI(
        uri.pathQueryRef.replace(/[&#]size=[^&]+$/, "")
      );
      let preferredSize = PlacesUtils.favicons.preferredSizeFromURI(uri);
      PlacesUtils.favicons.getFaviconDataForPage(
        pageURI,
        (iconURI, len, data, mimeType) => {
          if (len == 0) {
            streamDefaultFavicon(uri, loadInfo, pipe.outputStream, channel);
          } else {
            try {
              channel.contentType = mimeType;
              channel.contentLength = len;
              if (len != data.length) {
                throw new Error("Unexpected data length");
              }
              serveIcon(pipe, data);
            } catch (ex) {
              streamDefaultFavicon(uri, loadInfo, pipe.outputStream, channel);
            }
          }
        },
        preferredSize
      );

      return channel;
    } catch (ex) {
      return makeDefaultFaviconChannel(uri, loadInfo);
    }
  },

  allowPort(port, scheme) {
    return false;
  },

  classID: Components.ID("{60a1f7c6-4ff9-4a42-84d3-5a185faa6f32}"),
  QueryInterface: ChromeUtils.generateQI([
    Ci.nsIProtocolHandler,
    Ci.nsISupportsWeakReference,
  ]),
};

var EXPORTED_SYMBOLS = ["PageIconProtocolHandler"];
