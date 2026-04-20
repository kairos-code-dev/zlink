/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.BindException;
import dev.kairoscode.zlink.ZlinkException;
import java.io.IOException;
import java.net.SocketException;
import java.util.Locale;

final class PerfPolicy {
    private static final int ERRNO_EPERM = 1;
    private static final int ERRNO_EADDRINUSE = 98;
    private static final int ERRNO_ENOTSUP = 95;

    private PerfPolicy() {
    }

    static void validateMultiRecvMode(PerfUtil.Config config) {
        String recvMode = config.recvMode();
        if (!"recv".equalsIgnoreCase(recvMode)) {
            throw new IllegalArgumentException(
                "unsupported recv mode for " + config.pattern() + ": " + recvMode
                    + " (expected recv)");
        }
    }

    static PerfUtil.Result classifyFailure(PerfUtil.Config config, Throwable failure) {
        String unsupportedReason = unsupportedReason(failure);
        if (unsupportedReason != null) {
            return PerfUtil.Result.unsupported(unsupportedReason, config);
        }
        return new PerfUtil.Result("fail", sanitizeReason(failure), config.pattern(),
            config.transport(), config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
    }

    private static String unsupportedReason(Throwable failure) {
        for (Throwable current = failure; current != null; current = current.getCause()) {
            if (current instanceof BindException) {
                int errno = ((BindException) current).getInternalErrno();
                return errno > 0 ? "bind_errno_" + errno : "bind_failed";
            }
            if (current instanceof ZlinkException) {
                int errno = ((ZlinkException) current).getInternalErrno();
                if (errno == ERRNO_EPERM || errno == ERRNO_EADDRINUSE
                    || errno == ERRNO_ENOTSUP) {
                    return "bind_errno_" + errno;
                }
            }
            if (current instanceof SocketException || current instanceof IOException) {
                String reason = messageReason(current.getMessage());
                if (reason != null) {
                    return reason;
                }
            }
            String reason = messageReason(current.getMessage());
            if (reason != null) {
                return reason;
            }
        }
        return null;
    }

    private static String sanitizeReason(Throwable failure) {
        String message = failure == null ? null : failure.getMessage();
        if (message == null || message.isBlank()) {
            return failure == null ? "unknown_error"
                : failure.getClass().getSimpleName().toLowerCase(Locale.ROOT);
        }
        return message.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]+", "_")
            .replaceAll("^_+|_+$", "");
    }

    private static String messageReason(String message) {
        if (message == null || message.isBlank()) {
            return null;
        }
        String normalized = message.toLowerCase(Locale.ROOT);
        if (normalized.contains("operation not permitted")) {
            return "bind_operation_not_permitted";
        }
        if (normalized.contains("address already in use")) {
            return "bind_address_in_use";
        }
        if (normalized.contains("permission denied")) {
            return "bind_permission_denied";
        }
        if (normalized.contains("cannot assign requested address")) {
            return "bind_cannot_assign_address";
        }
        if (normalized.contains("protocol not supported")) {
            return "protocol_not_supported";
        }
        return null;
    }
}
