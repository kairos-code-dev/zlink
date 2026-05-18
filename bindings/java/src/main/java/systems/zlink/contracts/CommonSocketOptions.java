/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.SocketOptions;
import java.time.Duration;
import java.util.Objects;

public class CommonSocketOptions {
    final Socket socket;

    CommonSocketOptions(Socket socket) {
        this.socket = socket;
    }

    long affinity() {
        return socket.getOption(SocketOptions.AFFINITY);
    }

    void affinity(long value) {
        socket.setOption(SocketOptions.AFFINITY, value);
    }

    int rate() {
        return socket.getOption(SocketOptions.RATE);
    }

    void rate(int value) {
        socket.setOption(SocketOptions.RATE, value);
    }

    Duration recoveryInterval() {
        return Duration.ofMillis(socket.getOption(SocketOptions.RECOVERY_IVL));
    }

    void recoveryInterval(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.RECOVERY_IVL, toIntMillis(value, "value"));
    }

    public Duration linger() {
        return Duration.ofMillis(socket.getOption(SocketOptions.LINGER));
    }

    public void linger(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.LINGER, toIntMillis(value, "value"));
    }

    public int sendHwm() {
        return socket.getOption(SocketOptions.SNDHWM);
    }

    public void sendHwm(int value) {
        socket.setOption(SocketOptions.SNDHWM, value);
    }

    public int recvHwm() {
        return socket.getOption(SocketOptions.RCVHWM);
    }

    public void recvHwm(int value) {
        socket.setOption(SocketOptions.RCVHWM, value);
    }

    public int sendBuffer() {
        return socket.getOption(SocketOptions.SNDBUF);
    }

    public void sendBuffer(int value) {
        socket.setOption(SocketOptions.SNDBUF, value);
    }

    public int recvBuffer() {
        return socket.getOption(SocketOptions.RCVBUF);
    }

    public void recvBuffer(int value) {
        socket.setOption(SocketOptions.RCVBUF, value);
    }

    public Duration sendTimeout() {
        return Duration.ofMillis(socket.getOption(SocketOptions.SNDTIMEO));
    }

    public void sendTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.SNDTIMEO, toIntMillis(value, "value"));
    }

    public Duration recvTimeout() {
        return Duration.ofMillis(socket.getOption(SocketOptions.RCVTIMEO));
    }

    public void recvTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.RCVTIMEO, toIntMillis(value, "value"));
    }

    Duration handshakeInterval() {
        return Duration.ofMillis(socket.getOption(SocketOptions.HANDSHAKE_IVL));
    }

    void handshakeInterval(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.HANDSHAKE_IVL, toIntMillis(value, "value"));
    }

    public boolean immediate() {
        return socket.getOption(SocketOptions.IMMEDIATE) != 0;
    }

    public void immediate(boolean enabled) {
        socket.setOption(SocketOptions.IMMEDIATE, enabled ? 1 : 0);
    }

    public RidDuplicatePolicy ridDuplicatePolicy() {
        return RidDuplicatePolicy.fromValue(
          socket.getOption(SocketOptions.RID_DUPLICATE_POLICY));
    }

    public void ridDuplicatePolicy(RidDuplicatePolicy value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.RID_DUPLICATE_POLICY, value.value());
    }

    int routeValueMaxSize() {
        return socket.getOption(SocketOptions.ROUTE_VALUE_MAX_SIZE);
    }

    void routeValueMaxSize(int value) {
        socket.setOption(SocketOptions.ROUTE_VALUE_MAX_SIZE, value);
    }

    public Duration connectTimeout() {
        return Duration.ofMillis(socket.getOption(SocketOptions.CONNECT_TIMEOUT));
    }

    public void connectTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.CONNECT_TIMEOUT, toIntMillis(value, "value"));
    }

    public boolean ipv6() {
        return socket.getOption(SocketOptions.IPV6) != 0;
    }

    public void ipv6(boolean enabled) {
        socket.setOption(SocketOptions.IPV6, enabled ? 1 : 0);
    }

    int tos() {
        return socket.getOption(SocketOptions.TOS);
    }

    void tos(int value) {
        socket.setOption(SocketOptions.TOS, value);
    }

    int multicastHops() {
        return socket.getOption(SocketOptions.MULTICAST_HOPS);
    }

    void multicastHops(int value) {
        socket.setOption(SocketOptions.MULTICAST_HOPS, value);
    }

    int multicastMaxTpdu() {
        return socket.getOption(SocketOptions.MULTICAST_MAXTPDU);
    }

    void multicastMaxTpdu(int value) {
        socket.setOption(SocketOptions.MULTICAST_MAXTPDU, value);
    }

    String bindToDevice() {
        return socket.getOption(SocketOptions.BINDTODEVICE);
    }

    void bindToDevice(String value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.BINDTODEVICE, value);
    }

    public boolean tcpNoDelay() {
        return socket.getOption(SocketOptions.TCP_NODELAY) != 0;
    }

    public void tcpNoDelay(boolean enabled) {
        socket.setOption(SocketOptions.TCP_NODELAY, enabled ? 1 : 0);
    }

    public int tcpKeepalive() {
        return socket.getOption(SocketOptions.TCP_KEEPALIVE);
    }

    public void tcpKeepalive(int value) {
        socket.setOption(SocketOptions.TCP_KEEPALIVE, value);
    }

    int tcpKeepaliveCount() {
        return socket.getOption(SocketOptions.TCP_KEEPALIVE_CNT);
    }

    void tcpKeepaliveCount(int value) {
        socket.setOption(SocketOptions.TCP_KEEPALIVE_CNT, value);
    }

    int tcpKeepaliveIdle() {
        return socket.getOption(SocketOptions.TCP_KEEPALIVE_IDLE);
    }

    void tcpKeepaliveIdle(int value) {
        socket.setOption(SocketOptions.TCP_KEEPALIVE_IDLE, value);
    }

    int tcpKeepaliveInterval() {
        return socket.getOption(SocketOptions.TCP_KEEPALIVE_INTVL);
    }

    void tcpKeepaliveInterval(int value) {
        socket.setOption(SocketOptions.TCP_KEEPALIVE_INTVL, value);
    }

    int tcpMaxRt() {
        return socket.getOption(SocketOptions.TCP_MAXRT);
    }

    void tcpMaxRt(int value) {
        socket.setOption(SocketOptions.TCP_MAXRT, value);
    }

    public Duration heartbeatInterval() {
        return Duration.ofMillis(socket.getOption(SocketOptions.HEARTBEAT_IVL));
    }

    public void heartbeatInterval(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.HEARTBEAT_IVL, toIntMillis(value, "value"));
    }

    public Duration heartbeatTtl() {
        return Duration.ofMillis(socket.getOption(SocketOptions.HEARTBEAT_TTL));
    }

    public void heartbeatTtl(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.HEARTBEAT_TTL, toIntMillis(value, "value"));
    }

    public Duration heartbeatTimeout() {
        return Duration.ofMillis(socket.getOption(SocketOptions.HEARTBEAT_TIMEOUT));
    }

    public void heartbeatTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.HEARTBEAT_TIMEOUT, toIntMillis(value, "value"));
    }

    boolean conflate() {
        return socket.getOption(SocketOptions.CONFLATE) != 0;
    }

    void conflate(boolean enabled) {
        socket.setOption(SocketOptions.CONFLATE, enabled ? 1 : 0);
    }

    boolean blocky() {
        return socket.getOption(SocketOptions.BLOCKY) != 0;
    }

    void blocky(boolean enabled) {
        socket.setOption(SocketOptions.BLOCKY, enabled ? 1 : 0);
    }

    boolean invertMatching() {
        return socket.getOption(SocketOptions.INVERT_MATCHING) != 0;
    }

    void invertMatching(boolean enabled) {
        socket.setOption(SocketOptions.INVERT_MATCHING, enabled ? 1 : 0);
    }

    public long maxMsgSize() {
        return socket.getOption(SocketOptions.MAXMSGSIZE);
    }

    public void maxMsgSize(long value) {
        socket.setOption(SocketOptions.MAXMSGSIZE, value);
    }

    public int backlog() {
        return socket.getOption(SocketOptions.BACKLOG);
    }

    public void backlog(int value) {
        socket.setOption(SocketOptions.BACKLOG, value);
    }

    public Duration reconnectInterval() {
        return Duration.ofMillis(socket.getOption(SocketOptions.RECONNECT_IVL));
    }

    public void reconnectInterval(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.RECONNECT_IVL, toIntMillis(value, "value"));
    }

    public Duration reconnectIntervalMax() {
        return Duration.ofMillis(socket.getOption(SocketOptions.RECONNECT_IVL_MAX));
    }

    public void reconnectIntervalMax(Duration value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.RECONNECT_IVL_MAX, toIntMillis(value, "value"));
    }

    int fd() {
        return socket.getOption(SocketOptions.FD);
    }

    int events() {
        return socket.getOption(SocketOptions.EVENTS);
    }

    SocketType socketType() {
        return SocketType.fromValue(socket.getOption(SocketOptions.TYPE));
    }

    public String lastEndpoint() {
        return socket.getOption(SocketOptions.LAST_ENDPOINT);
    }

    void zmpMetadata(String value) {
        Objects.requireNonNull(value, "value");
        socket.setOption(SocketOptions.ZMP_METADATA, value);
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
