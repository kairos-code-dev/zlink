/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import java.time.Duration;

/**
 * Perf-only bridge for package-private common socket option access.
 */
public final class PerfSocketOptions {
    private PerfSocketOptions() {
    }

    public static void sendHwm(Socket socket, int value) {
        socket.options().sendHwm(value);
    }

    public static void recvHwm(Socket socket, int value) {
        socket.options().recvHwm(value);
    }

    public static void sendTimeout(Socket socket, Duration value) {
        socket.options().sendTimeout(value);
    }

    public static void recvTimeout(Socket socket, Duration value) {
        socket.options().recvTimeout(value);
    }

    public static void linger(Socket socket, Duration value) {
        socket.options().linger(value);
    }
}
