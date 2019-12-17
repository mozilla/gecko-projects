# cubeb-coreaudio-rs

[![Build Status](https://travis-ci.org/ChunMinChang/cubeb-coreaudio-rs.svg?branch=trailblazer)](https://travis-ci.org/ChunMinChang/cubeb-coreaudio-rs)

*Rust* implementation of [Cubeb][cubeb] on [the MacOS platform][cubeb-au].

## Current Goals
- Keep refactoring the implementation until it looks rusty! (it's translated from C at first.)
  - Check the [todo list][todo] first

## Status

The code is currently tested in the _Firefox Nightly_ under a _perf_.

- Try it:
  - Open `about:config`
  - Add a perf `media.cubeb.backend` with string `audiounit-rust`
  - Restart Firefox Nightly
  - Open `about:support`
  - Check if the `Audio Backend` in `Media` section is `audiounit-rust` or not
  - Retart Firefox Nightly again if it's not.

## Test
Please run `sh run_tests.sh`.

Some tests cannot be run in parallel.
They may operate the same device at the same time,
or indirectly fire some system events that are listened by some tests.

The tests that may affect others are marked `#[ignore]`.
They will be run by `cargo test ... -- --ignored ...`
after finishing normal tests.
Most of the tests are executed in `run_tests.sh`.
Only those tests commented with *FIXIT* are left.

### Device Switching
The system default device will be changed during our tests.
All the available devices will take turns being the system default device.
However, after finishing the tests, the default device will be set to the original one.
The sounds in the tests should be able to continue whatever the system default device is.

### Device Plugging/Unplugging
We implement APIs simulating plugging or unplugging a device
by adding or removing an aggregate device programmatically.
It's used to verify our callbacks for minitoring the system devices work.

### Manual Test
- Output devices switching
  - `$ cargo test test_switch_output_device -- --ignored --nocapture`
  - Enter `s` to switch output devices
  - Enter `q` to finish test
- Device change events listener
  - `$ cargo test test_add_then_remove_listeners -- --ignored --nocapture`
  - Plug/Unplug devices or switch input/output devices to see events log.
- Device collection change
  - `cargo test test_device_collection_change -- --ignored --nocapture`
  - Plug/Unplug devices to see events log.

## TODO
See [todo list][todo]

## Issues
- Atomic:
  - We need atomic type around `f32` but there is no this type in the stardard Rust
  - Using [atomic-rs](https://github.com/Amanieu/atomic-rs) to do this.
- No guarantee on `audiounit_set_channel_layout`
  - This call doesn't work all the times
  - Returned `NO_ERR` doesn't guarantee the layout is set to the one we want
  - The layouts on some devices won't be changed even no errors are returned,
    e.g., we can set _stereo_ layout to a _4-channels aggregate device_ with _QUAD_ layout
    (created by Audio MIDI Setup) without any error. However, the layout
    of this 4-channels aggregate device is still QUAD after setting it without error
  - Another weird thing is that we will get a `kAudioUnitErr_InvalidPropertyValue`
    if we set the layout to _QUAD_. It's the same layout as its original one but it cannot be set!
- `kAudioDevicePropertyBufferFrameSize` cannot be set when another stream using the same device with smaller buffer size is active. See [here][chg-buf-sz] for details.

### Test issues
- Fail to run tests that depend on `AggregateDevice::create_blank_device` with the tests that work with the device event listeners
  - The `AggregateDevice::create_blank_device` will add an aggregate device to the system and fire the device-change events indirectly.
- `TestDeviceSwitcher` cannot work when there is an alive full-duplex stream
  - An aggregate device will be created for a duplex stream when its input and output devices are different.
  - `TestDeviceSwitcher` will cached the available devices, upon it's created, as the candidates for default device
  - Hence the created aggregate device may be cached in `TestDeviceSwitcher`
  - If the aggregate device is destroyed (when the destroying the duplex stream created it) but the `TestDeviceSwitcher` is still working,
    it will set a destroyed device as the default device
  - See details in [device_change.rs](src/backend/tests/device_change.rs)

## Branches
- [trailblazer][trailblazer]: Main branch
- [plain-translation-from-c][from-c]: The code is rewritten from C code on a line-by-line basis
- [ocs-disposal][ocs-disposal]: The first version that replace our custom mutex by Rust Mutex

[cubeb]: https://github.com/kinetiknz/cubeb "Cross platform audio library"
[cubeb-au]: https://github.com/kinetiknz/cubeb/blob/master/src/cubeb_audiounit.cpp "Cubeb AudioUnit"

[chg-buf-sz]: https://cs.chromium.org/chromium/src/media/audio/mac/audio_manager_mac.cc?l=982-989&rcl=0207eefb445f9855c2ed46280cb835b6f08bdb30 "issue on changing buffer size"

[todo]: todo.md

[bmo1572273]: https://bugzilla.mozilla.org/show_bug.cgi?id=1572273
[bmo1572273-c13]: https://bugzilla.mozilla.org/show_bug.cgi?id=1572273#c13

[from-c]: https://github.com/ChunMinChang/cubeb-coreaudio-rs/tree/plain-translation-from-c
[ocs-disposal]: https://github.com/ChunMinChang/cubeb-coreaudio-rs/tree/ocs-disposal
[trailblazer]: https://github.com/ChunMinChang/cubeb-coreaudio-rs/tree/trailblazer