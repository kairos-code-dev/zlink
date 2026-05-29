/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

public final class NativeErrno {
    public static final int ENOENT = 2;
    public static final int EINTR = 4;
    public static final int EAGAIN = 11;
    public static final int EINVAL = 22;
    public static final int ENOTCONN = 107;
    public static final int EHOSTUNREACH = 113;
    public static final int EWOULDBLOCK_WIN = 10035;
    public static final int ENOTCONN_WIN = 10057;
    public static final int EHOSTUNREACH_WIN = 10065;

    private NativeErrno() {
    }
}
