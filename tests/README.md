# CTest suite

Run from the PortForwarder directory with CMake, a C++17 compiler, and the
project's Boost.System dependency installed:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

There are exactly 108 independently registered cases: 40 Agent, 37 Server,
15 Tunnel, and 16 end-to-end tests. `cases.inc` is shared by the C++ runner and
CTest registration. Every case runs in a separate process; no test framework
download is required.

Tests exercise the root README's registration protocol, network byte order,
arbitrary trigger bytes, connection ordering, FIFO pairing, timeout handling,
failure isolation, lifecycle rules, and active tunnel survival. Binary payloads
include NUL and high-bit bytes, with sizes around the tunnel's 4096-byte buffer.
Additional cases cover invalid configurations and socket closure propagation.
Bind-address tests check each listener's address restriction, explicit wildcard
acceptance, independent addresses, and forwarding with configured addresses.

All network peers use IPv4 loopback and dynamically allocated ports. Bind-address
tests use `127.0.0.1`, `127.0.0.2`, and `127.0.0.3` within the loopback network.
Production objects and their `io_context` run on one thread. Socket operations have bounded
waits, and CTest enforces a 15-second limit per case. The default 30-second timeout
is checked through configuration and a short live wait; the custom timeout case
checks actual expiration at one second and cancellation after pairing.

Select cases with CTest labels or names:

```sh
ctest --test-dir build -L agent --output-on-failure
ctest --test-dir build -R server_pairs_waiting_clients_fifo --output-on-failure
ctest --test-dir build -j 4 --output-on-failure
```

Use `-DBUILD_TESTING=OFF` to build the libraries and executables without tests.
