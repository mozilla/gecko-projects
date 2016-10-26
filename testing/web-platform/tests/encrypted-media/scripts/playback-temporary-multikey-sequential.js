function runTest(config,qualifier) {

    // config.initData contains a list of keys. We expect those to be needed in order and get
    // one waitingforkey event for each one.

    var testname = testnamePrefix(qualifier, config.keysystem)
                                    + ', successful playback, temporary, '
                                    + /video\/([^;]*)/.exec(config.videoType)[1]
                                    + ', multiple keys, sequential'
                                    + (config.checkReadyState ? ', readyState' : '');

    var configuration = {   initDataTypes: [config.initDataType],
                            audioCapabilities: [{contentType: config.audioType}],
                            videoCapabilities: [{contentType: config.videoType}],
                            sessionTypes: ['temporary'] };

    async_test(function(test) {
        var _video = config.video,
            _mediaKeys,
            _mediaKeySessions = [],
            _mediaSource,
            _playbackStarted = false;

        function startNewSession() {
            assert_less_than(_mediaKeySessions.length, config.initData.length);
            var mediaKeySession = _mediaKeys.createSession('temporary');
            waitForEventAndRunStep('message', mediaKeySession, onMessage, test);
            _mediaKeySessions.push(mediaKeySession);
            mediaKeySession.variantId = config.variantIds ? config.variantIds[_mediaKeySessions.length - 1] : undefined;
            mediaKeySession.generateRequest(config.initDataType, config.initData[_mediaKeySessions.length - 1]).catch(onFailure);
        }

        function onFailure(error) {
            forceTestFailureFromPromise(test, error);
        }

        function onMessage(event) {
            config.messagehandler(event.messageType, event.message, {variantId: event.target.variantId}).then(function(response) {
                return event.target.update(response);
            }).catch(onFailure);
        }

        function onWaitingForKey(event) {
            if (config.checkReadyState) {
                assert_equals(_video.readyState, _video.HAVE_METADATA, "Video readyState should be HAVE_METADATA on watingforkey event");
            }
            startNewSession();
        }

        function onPlaying(event) {
            assert_equals(_mediaKeySessions.length, 1, "Playback should start with a single key / session");
        }

        function onTimeupdate(event) {
            if (_video.currentTime > config.duration) {
                assert_equals(_mediaKeySessions.length, config.initData.length, "It should require all keys to reach end of content");
                _video.pause();
                test.done();
            }
        }

        navigator.requestMediaKeySystemAccess(config.keysystem, [configuration]).then(function(access) {
            return access.createMediaKeys();
        }).then(function(mediaKeys) {
            _mediaKeys = mediaKeys;
            return _video.setMediaKeys(_mediaKeys);
        }).then(function(){
            // Not using waitForEventAndRunStep() to avoid too many
            // EVENT(onTimeUpdate) logs.
            _video.addEventListener('timeupdate', test.step_func(onTimeupdate), true);

            waitForEventAndRunStep('waitingforkey', _video, onWaitingForKey, test);
            waitForEventAndRunStep('playing', _video, onPlaying, test);

            startNewSession();

            return testmediasource(config);
        }).then(function(source) {
            _mediaSource = source;
            _video.src = URL.createObjectURL(_mediaSource);
            _video.play();
        }).catch(onFailure);
    }, testname);
}
