# USRP Test Code

# Building

[Install the UHD build
prerequisites](https://files.ettus.com/manual/page_build_guide.html) and type `make`.

# Goal

Our goal is to start the transmission at some offset from our 5ms frame cycle.
We'd prefer to be able to start at any frame, but the frame on the PPS edge is
acceptable.

## build/timed_tx (timed_tx.cpp)

Various tests to display USRP timing issues we've encountered on the B200
platform.

To invoke the test, run `build/timed_tx` with one of the following arguments
passed to `--test-method`.

For each test, we must set the requested start time to at least 2 seconds in the
future to avoid (L) timing errors. In *all* cases, it seems that the USRP is
actually transmiting ~2.5ms sooner than the expected time.

### tx_metadata

This sets the xmit time using the `has_time_spec` member of the tx_metadata_t
used in the send(). The transmits start at random times and are not consistent.

### pps_edge

This uses `usrp->set_time_unknown_pps(uhd::time_spec_t(0.0))` immediately before
the send. We must add an additional 17us to our send time in order to meet
timing. This results in consistently starting the transmit at the correct offset
from the frame start.

### burst_timing

This is a test of the send time. `has_time_spec`, `start_of_burst`, and
`end_of_burst` are all set. `usrp->set_time_now(0.0);` is called right before
the `send()`, but prior to the timer start. This shows that the send is
ocurring ~2.5ms before the time that is set.

### fast_timing

This adds the 2 second delay to the initial transmit, then transmits 50 times as
fast as possible. It shows that the `send()` takes slightly less than 5ms each
time. We would expect it to take 5ms each time.
