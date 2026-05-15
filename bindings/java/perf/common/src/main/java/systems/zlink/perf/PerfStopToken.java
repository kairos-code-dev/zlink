/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Message;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Wire-level stop token shared by single + multi perf benchmarks.
 *
 * <p>Senders emit the literal byte sequence {@code __zlink_perf_stop__} on the
 * wire once at the end of an active phase to signal phase completion to a
 * receiver waiting on a poller. Receivers exit when {@link #isStopToken(byte[])}
 * (or {@link #isStopTokenMessage(Message)}) returns {@code true}.
 *
 * <p>This mirrors the C++ {@code k_stop_token} / {@code is_stop_token} helpers
 * in {@code bindings/cpp/perf/single/common/perf_single_common.hpp} and the
 * multi-side {@code is_stop_token} helper, and follows the policy in
 * {@code doc/perf/PERF_SINGLE_TEST_POLICY.md} § 1.4 and
 * {@code doc/perf/PERF_MULTI_TEST_POLICY.md} § 1.3.1.
 *
 * <p>The token replaces {@code AtomicBoolean senderDone} + short polling
 * patterns. Receivers must use {@code Duration.ofMillis(-1)} (signal-driven
 * indefinite wait) and exit purely on stop-token arrival.
 */
public final class PerfStopToken {
    /** Literal stop-token byte sequence, shared across all bindings. */
    public static final byte[] BYTES =
        "__zlink_perf_stop__".getBytes(StandardCharsets.UTF_8);

    private PerfStopToken() {
    }

    /** Returns {@code true} if the supplied bytes are exactly the stop token. */
    public static boolean isStopToken(byte[] data) {
        return data != null && Arrays.equals(data, BYTES);
    }

    /** Returns {@code true} if the message frame contents are the stop token. */
    public static boolean isStopTokenMessage(Message message) {
        if (message == null) {
            return false;
        }
        if (message.size() != BYTES.length) {
            return false;
        }
        return Arrays.equals(message.data(), BYTES);
    }

    /** Allocates a new {@link Message} carrying the stop-token bytes. */
    public static Message newMessage() {
        return Message.copyOf(BYTES);
    }
}
