/*
 * Copyright 2017, Leanplum, Inc. All rights reserved.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
package com.leanplum.internal;

import android.os.AsyncTask;
import android.support.annotation.NonNull;

import com.leanplum.Leanplum;

import org.json.JSONObject;

import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;

/**
 * LeanplumEventCallbackManager class to handle event callbacks.
 *
 * @author Anna Orlova
 */
class LeanplumEventCallbackManager {
  // Event callbacks map.
  private final Map<RequestOld, LeanplumEventCallbacks> eventCallbacks = new HashMap<>();

  /**
   * Add callbacks to the event callbacks Map.
   *
   * @param request Event.
   * @param responseCallback Response callback.
   * @param errorCallback Error callback.
   */
  void addCallbacks(RequestOld request, RequestOld.ResponseCallback responseCallback,
                    RequestOld.ErrorCallback errorCallback) {
    if (request == null) {
      return;
    }

    if (responseCallback == null && errorCallback == null) {
      return;
    }

    eventCallbacks.put(request, new LeanplumEventCallbacks(responseCallback, errorCallback));
    Leanplum.countAggregator().incrementCount("add_event_callback_at");
  }

  /**
   * Invoke potential error callbacks for all events with database index less than a count of events
   * that we got from database.
   *
   * @param error Exception.
   * @param countOfEvents Count of events that we got from database.
   */
  void invokeAllCallbacksWithError(@NonNull final Exception error, int countOfEvents) {
    if (eventCallbacks.size() == 0) {
      return;
    }

    Iterator<Map.Entry<RequestOld, LeanplumEventCallbacks>> iterator =
        eventCallbacks.entrySet().iterator();
    // Loop over all callbacks.
    for (; iterator.hasNext(); ) {
      final Map.Entry<RequestOld, LeanplumEventCallbacks> entry = iterator.next();
      if (entry.getKey() == null) {
        continue;
      }
      if (entry.getKey().getDataBaseIndex() >= countOfEvents) {
        entry.getKey().setDataBaseIndex(entry.getKey().getDataBaseIndex() - countOfEvents);
      } else {
        if (entry.getValue() != null && entry.getValue().errorCallback != null) {
          // Start callback asynchronously, to avoid creation of new RequestOld object from the same
          // thread.
          Util.executeAsyncTask(false, new AsyncTask<Void, Void, Void>() {
            @Override
            protected Void doInBackground(Void... params) {
              entry.getValue().errorCallback.error(error);
              return null;
            }
          });
        }
        iterator.remove();
      }
    }
    Leanplum.countAggregator().incrementCount("invoke_error_callbacks_on_responses");
  }

  /**
   * Invoke potential response callbacks for all events with database index less than a count of
   * events that we got from database.
   *
   * @param responseBody JSONObject withs server response.
   * @param countOfEvents Count of events that we got from database.
   */
  void invokeAllCallbacksForResponse(@NonNull final JSONObject responseBody, int countOfEvents) {
    if (eventCallbacks.size() == 0) {
      return;
    }

    Iterator<Map.Entry<RequestOld, LeanplumEventCallbacks>> iterator =
        eventCallbacks.entrySet().iterator();
    // Loop over all callbacks.
    for (; iterator.hasNext(); ) {
      final Map.Entry<RequestOld, LeanplumEventCallbacks> entry = iterator.next();
      if (entry.getKey() == null) {
        continue;
      }

      if (entry.getKey().getDataBaseIndex() >= countOfEvents) {
        entry.getKey().setDataBaseIndex(entry.getKey().getDataBaseIndex() - countOfEvents);
      } else {
        if (entry.getValue() != null && entry.getValue().responseCallback != null) {
          // Start callback asynchronously, to avoid creation of new RequestOld object from the same
          // thread.
          Util.executeAsyncTask(false, new AsyncTask<Void, Void, Void>() {
            @Override
            protected Void doInBackground(Void... params) {
              entry.getValue().responseCallback.response(RequestOld.getResponseAt(responseBody,
                  (int) entry.getKey().getDataBaseIndex()));
              return null;
            }
          });
        }
        iterator.remove();
      }
    }
    Leanplum.countAggregator().incrementCount("invoke_success_callbacks_on_responses");
  }

  private static class LeanplumEventCallbacks {
    private RequestOld.ResponseCallback responseCallback;
    private RequestOld.ErrorCallback errorCallback;

    LeanplumEventCallbacks(RequestOld.ResponseCallback responseCallback, RequestOld.ErrorCallback
        errorCallback) {
      this.responseCallback = responseCallback;
      this.errorCallback = errorCallback;
    }
  }
}
