/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;

public final class ZlinkException extends RuntimeException {
    private final int errno;
    private final ErrorCode errorCode;

    private ZlinkException(String message, int errno, ErrorCode errorCode) {
        super(message);
        this.errno = errno;
        this.errorCode = errorCode;
    }

    public int errno() {
        return errno;
    }

    public ErrorCode errorCode() {
        return errorCode;
    }

    public static ZlinkException fromLastError(String operation) {
        int errno = safeErrno();
        return fromErrno(operation, errno);
    }

    public static ZlinkException fromErrno(String operation, int errno) {
        String detail = safeStrerror(errno);
        ErrorCode code = mapErrorCode(errno);
        String op = operation == null || operation.isEmpty()
            ? "zlink operation failed" : operation + " failed";
        String message = detail.isEmpty()
            ? op + " (errno=" + errno + ")"
            : op + ": " + detail + " (errno=" + errno + ")";
        return new ZlinkException(message, errno, code);
    }

    private static int safeErrno() {
        try {
            return Native.errno();
        } catch (RuntimeException ignored) {
            return -1;
        }
    }

    private static String safeStrerror(int errno) {
        if (errno < 0)
            return "";
        try {
            String msg = Native.strerror(errno);
            return msg == null ? "" : msg;
        } catch (RuntimeException ignored) {
            return "";
        }
    }

    private static ErrorCode mapErrorCode(int errno) {
        for (ErrorCode code : ErrorCode.values()) {
            if (code.getValue() == errno)
                return code;
        }
        return null;
    }
}
