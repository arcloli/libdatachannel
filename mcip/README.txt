MCIP RTCP receiver candidate 1 (2026-11-07)

Based on upstream libdatachannel v0.23.1, commit
222529eb2c8ae44f96462504ae38023f62809cec. Upstream history, license,
notices and dependency gitlinks are preserved. Added MCIP test/support
files use the repository's MPL-2.0 license. This is an offline-validated
source candidate, not a board-validated firmware release.

Changes
- Correct RTCP receiver extended sequence, interval fraction loss, signed
  cumulative loss, jitter, LSR and DLSR reporting.
- RFC 3550 sequence probation/restart accounting; one active source with
  bounded state; SSRC changes reset reception statistics.
- Validate RTP/RTCP sizes before reading packet fields. Callbacks execute
  outside the statistics lock; SDP clock mappings use fixed-size storage.
- Keep upstream preparePacket API and add explicit prepareReceptionReport
  and signed loss accessor. Unknown clock rate means jitter is unmeasured.
- Private Linux SONAME libdatachannel-mcip.so.0.23-mcip1 prevents accidental
  substitution for a different C++ ABI. Actual file: .so.0.23.1.
  Rebuild consumers against these headers and link -ldatachannel-mcip.

Clone and build (Linux)
  git clone --recurse-submodules git@github.com:arcloli/libdatachannel.git
  cd libdatachannel
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DNO_EXAMPLES=ON -DNO_TESTS=ON -DUSE_NICE=OFF \
    -DBUILD_SHARED_LIBS=ON -DRTC_UPDATE_VERSION_HEADER=OFF
  cmake --build build --target datachannel -j4
  ./mcip/run-tests.sh build

Requires a C++17 compiler, CMake, make/ninja, OpenSSL development files,
and Git submodules. For a non-system OpenSSL installation, pass
-DOPENSSL_ROOT_DIR=/path/to/openssl to CMake. The tests below do not need
the browser/application source. For a target SDK, pass its CMake toolchain
file; execute target fixtures only under a matching target or emulator.
The CMake target remains datachannel despite the private output name.

Tests
run-tests.sh builds all three fixtures, then runs socket-free RTCP and
NACK tests with time limits. Its output defaults to build/mcip-tests.
- rtcp_reports: 125 wire assertions plus 10,000 deterministic malformed
  inputs, including wrap/reorder/duplicates/restart, clock mapping, SR
  timing, signed saturation and callback re-entry.
- nack-window: characterizes the unchanged 512-packet retransmission
  window; passing this test does not mean that window is sufficient.
- loopback: two peers, three synthetic SRTP packets, one SRTCP RR and one
  SCTP message; no decoder. Compile only by default. Run separately in a
  private network namespace, never against the board. On Linux with
  unprivileged namespaces enabled, for example:
    timeout 30 unshare --user --map-root-user --net sh -c \
      'ip link set lo up; exec build/mcip-tests/loopback'
  Namespace setup failure is not a test pass. No STUN/TURN is configured.

For sanitizer validation, build the entire library with
-fsanitize=address,undefined in C/CXX and shared linker flags. Then run:
  SANITIZE=1 ./mcip/run-tests.sh build-sanitized
This enables address/undefined checks and leak detection for the fixtures.

Recorded validation (evidence/ contains bounded original fixture output)
- Native and AArch64/QEMU RTCP tests passed.
- Native ASan/UBSan/LeakSanitizer run passed.
- Isolated loopback DTLS/SRTP, SRTCP, SCTP and <=5-second cleanup passed.
- A downstream AArch64 browser clean build linked successfully: all 75 rtc
  imports resolved; all 98 original C API symbols were retained. Proprietary
  application sources/binaries and the unknown-source old library are not
  part of this repository. A successful link is not board validation.
source-binding.json binds the five modified library files to the validated
snapshot. Evidence is historical; no board deployment was performed.

Limits / unresolved issues
This does not establish or solve the remote-video-stutter root cause.
The inspected sender's outbound path does not use this receiving session.
The 512-packet NACK window, per-packet SRTP copy/encryption/send lock,
send-result propagation and long-run memory behavior remain unchanged.
Real decoder quality, hardware acceleration, audio, local HDMI and board
performance still require separate controlled validation. No periodic
RTCP timer, jitter buffer or unbounded per-source map is introduced.
