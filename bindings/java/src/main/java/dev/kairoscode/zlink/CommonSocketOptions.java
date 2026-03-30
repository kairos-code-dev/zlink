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
        receiveTimeoutMillis(Math.toIntExact(timeout.toMillis()));
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
        sendTimeoutMillis(Math.toIntExact(timeout.toMillis()));
    }
}
