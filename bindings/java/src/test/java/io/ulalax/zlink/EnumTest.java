package io.ulalax.zlink;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

public class EnumTest {
    @Test
    public void socketTypeValues() {
        assertEquals(0, SocketType.PAIR.getValue());
        assertEquals(1, SocketType.PUB.getValue());
        assertEquals(2, SocketType.SUB.getValue());
        assertEquals(5, SocketType.DEALER.getValue());
        assertEquals(6, SocketType.ROUTER.getValue());
        assertEquals(9, SocketType.XPUB.getValue());
        assertEquals(10, SocketType.XSUB.getValue());
        assertEquals(11, SocketType.STREAM.getValue());
    }

    @Test
    public void contextOptionValues() {
        assertEquals(1, ContextOption.IO_THREADS.getValue());
        assertEquals(2, ContextOption.MAX_SOCKETS.getValue());
        assertEquals(9, ContextOption.THREAD_NAME_PREFIX.getValue());
    }

    @Test
    public void socketOptionValues() {
        assertEquals(17, SocketOption.LINGER.getValue());
        assertEquals(23, SocketOption.SNDHWM.getValue());
        assertEquals(24, SocketOption.RCVHWM.getValue());
        assertEquals(6, SocketOption.SUBSCRIBE.getValue());
        assertEquals(95, SocketOption.TLS_CERT.getValue());
        assertEquals(102, SocketOption.TLS_PASSWORD.getValue());
        assertEquals(117, SocketOption.ZMP_METADATA.getValue());
    }

    @Test
    public void sendFlagValues() {
        assertEquals(0, SendFlag.NONE.getValue());
        assertEquals(1, SendFlag.DONTWAIT.getValue());
        assertEquals(2, SendFlag.SNDMORE.getValue());
    }

    @Test
    public void receiveFlagValues() {
        assertEquals(0, ReceiveFlag.NONE.getValue());
        assertEquals(1, ReceiveFlag.DONTWAIT.getValue());
    }

    @Test
    public void monitorEventTypeValues() {
        assertEquals(0x0001, MonitorEventType.CONNECTED.getValue());
        assertEquals(0x0200, MonitorEventType.DISCONNECTED.getValue());
        assertEquals(0xFFFF, MonitorEventType.ALL.getValue());
    }

    @Test
    public void disconnectReasonValues() {
        assertEquals(0, DisconnectReason.UNKNOWN.getValue());
        assertEquals(5, DisconnectReason.CTX_TERM.getValue());
    }

    @Test
    public void pollEventTypeValues() {
        assertEquals(1, PollEventType.POLLIN.getValue());
        assertEquals(2, PollEventType.POLLOUT.getValue());
        assertEquals(4, PollEventType.POLLERR.getValue());
        assertEquals(8, PollEventType.POLLPRI.getValue());
    }

    @Test
    public void serviceTypeValues() {
        assertEquals(1, ServiceType.GATEWAY.getValue());
        assertEquals(2, ServiceType.SPOT.getValue());
    }

    @Test
    public void gatewayLbStrategyValues() {
        assertEquals(0, GatewayLbStrategy.ROUND_ROBIN.getValue());
        assertEquals(1, GatewayLbStrategy.WEIGHTED.getValue());
    }

    @Test
    public void socketRoleValues() {
        assertEquals(1, RegistrySocketRole.PUB.getValue());
        assertEquals(2, RegistrySocketRole.ROUTER.getValue());
        assertEquals(3, RegistrySocketRole.PEER_SUB.getValue());
        assertEquals(1, DiscoverySocketRole.SUB.getValue());
        assertEquals(1, GatewaySocketRole.ROUTER.getValue());
        assertEquals(1, ReceiverSocketRole.ROUTER.getValue());
        assertEquals(2, ReceiverSocketRole.DEALER.getValue());
        assertEquals(1, SpotNodeSocketRole.PUB.getValue());
        assertEquals(2, SpotNodeSocketRole.SUB.getValue());
        assertEquals(3, SpotNodeSocketRole.DEALER.getValue());
        assertEquals(1, SpotSocketRole.PUB.getValue());
        assertEquals(2, SpotSocketRole.SUB.getValue());
    }

    @Test
    public void flagCombine() {
        assertEquals(3, SendFlag.combine(SendFlag.DONTWAIT, SendFlag.SNDMORE));
        assertEquals(0x0201, MonitorEventType.combine(
            MonitorEventType.CONNECTED, MonitorEventType.DISCONNECTED));
        assertEquals(3, PollEventType.combine(PollEventType.POLLIN, PollEventType.POLLOUT));
    }
}
