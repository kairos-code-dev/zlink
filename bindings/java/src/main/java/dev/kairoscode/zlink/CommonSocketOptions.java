/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.options.SocketOptions;
import java.time.Duration;
import java.util.Objects;

public class CommonSocketOptions {
    final Socket socket;

    CommonSocketOptions(Socket socket) {
        this.socket = socket;
    }

    public int receiveTimeoutMillis() {
        return socket.getOption(SocketOptions.RCVTIMEO);
    }

    public Duration receiveTimeout() {
        return Duration.ofMillis(receiveTimeoutMillis());
    }

    public void receiveTimeoutMillis(int timeoutMillis) {
        socket.setOption(SocketOptions.RCVTIMEO, timeoutMillis);
    }

    public void receiveTimeout(Duration timeout) {
        Objects.requireNonNull(timeout, "timeout");
        receiveTimeoutMillis(toIntMillis(timeout, "timeout"));
    }

    public int sendTimeoutMillis() {
        return socket.getOption(SocketOptions.SNDTIMEO);
    }

    public Duration sendTimeout() {
        return Duration.ofMillis(sendTimeoutMillis());
    }

    public void sendTimeoutMillis(int timeoutMillis) {
        socket.setOption(SocketOptions.SNDTIMEO, timeoutMillis);
    }

    public void sendTimeout(Duration timeout) {
        Objects.requireNonNull(timeout, "timeout");
        sendTimeoutMillis(toIntMillis(timeout, "timeout"));
    }

    private static int toIntMillis(Duration timeout, String name) {
        long millis = timeout.toMillis();
        if (millis < Integer.MIN_VALUE || millis > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(name + " millis out of int range: "
                + millis);
        }
        return (int) millis;
    }
}
