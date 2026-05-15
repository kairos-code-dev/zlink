/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativebridge;

import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;

public final class NativeSubmitErrors {
    public static final int EAGAIN = 11;
    public static final int EWOULDBLOCK_WIN = 10035;
    public static final int ENOTCONN = 107;
    public static final int ENOTCONN_WIN = 10057;
    public static final int EHOSTUNREACH = 113;
    public static final int EHOSTUNREACH_WIN = 10065;
    public static final int ECONNREFUSED = 111;
    public static final int ECONNREFUSED_WIN = 10061;

    private NativeSubmitErrors() {
    }

    public static boolean isBackpressured(int errno) {
        return errno == EAGAIN || errno == EWOULDBLOCK_WIN;
    }

    public static boolean isNotConnected(int errno) {
        return errno == ENOTCONN || errno == ENOTCONN_WIN
            || errno == EHOSTUNREACH || errno == EHOSTUNREACH_WIN;
    }

    public static boolean isNotAdmitted(int errno) {
        return errno == ECONNREFUSED || errno == ECONNREFUSED_WIN;
    }

    public static SubmitException submitExceptionOrNull(int errno) {
        if (isBackpressured(errno)) {
            return new SubmitException(SubmitResult.BACKPRESSURED, errno);
        }
        if (isNotConnected(errno)) {
            return new SubmitException(SubmitResult.NOT_CONNECTED, errno);
        }
        if (isNotAdmitted(errno)) {
            return new SubmitException(SubmitResult.NOT_ADMITTED, errno);
        }
        return null;
    }
}
