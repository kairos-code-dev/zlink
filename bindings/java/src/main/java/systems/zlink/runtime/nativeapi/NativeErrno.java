/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.util.function.IntPredicate;
import java.util.function.IntSupplier;

public final class NativeErrno {
    public static final int ENOENT = 2;
    public static final int EINTR = 4;
    public static final int EBADF = 9;
    public static final int EAGAIN = 11;
    public static final int ENOMEM = 12;
    public static final int EFAULT = 14;
    public static final int EBUSY = 16;
    public static final int EINVAL = 22;
    public static final int EADDRINUSE = 98;
    public static final int ECONNREFUSED = 111;
    public static final int ENOTSUP = 95;
    public static final int ENOTCONN = 107;
    public static final int EHOSTUNREACH = 113;
    public static final int EWOULDBLOCK_WIN = 10035;
    public static final int ECONNREFUSED_WIN = 10061;
    public static final int ENOTCONN_WIN = 10057;
    public static final int EHOSTUNREACH_WIN = 10065;

    private NativeErrno() {
    }

    public static int retryWhileInterrupted(IntSupplier call,
                                            IntPredicate failed) {
        while (true) {
            int rc = call.getAsInt();
            if (!failed.test(rc) || Native.errno() != EINTR) {
                return rc;
            }
        }
    }
}
