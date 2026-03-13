package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.discovery.DiscoverySocketRole;
import dev.kairoscode.zlink.service.gateway.GatewayLbStrategy;
import dev.kairoscode.zlink.service.gateway.GatewaySocketRole;
import dev.kairoscode.zlink.service.receiver.ReceiverSocketRole;
import dev.kairoscode.zlink.service.registry.RegistrySocketRole;
import dev.kairoscode.zlink.service.spot.SpotNodeOption;
import dev.kairoscode.zlink.service.spot.SpotNodePubMode;
import dev.kairoscode.zlink.service.spot.SpotNodePubQueueFullPolicy;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;
import dev.kairoscode.zlink.service.spot.SpotSocketRole;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

public class CoreEnumPortedTest {
    @Test
    public void testSocketTypeValues() {
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
    public void testContextOptionValues() {
        assertEquals(1, ContextOption.IO_THREADS.getValue());
        assertEquals(2, ContextOption.MAX_SOCKETS.getValue());
        assertEquals(9, ContextOption.THREAD_NAME_PREFIX.getValue());
    }

    @Test
    public void testSocketOptionValues() {
        assertEquals(17, SocketOption.LINGER.getValue());
        assertEquals(23, SocketOption.SNDHWM.getValue());
        assertEquals(24, SocketOption.RCVHWM.getValue());
        assertEquals(6, SocketOption.SUBSCRIBE.getValue());
        assertEquals(95, SocketOption.TLS_CERT.getValue());
        assertEquals(102, SocketOption.TLS_PASSWORD.getValue());
        assertEquals(73, SocketOption.STREAM_NOTIFY.getValue());
        assertEquals(118, SocketOption.TCP_NODELAY.getValue());
        assertEquals(117, SocketOption.ZMP_METADATA.getValue());
    }

    @Test
    public void testSendReceiveFlagValues() {
        assertEquals(0, SendFlag.NONE.getValue());
        assertEquals(1, SendFlag.DONTWAIT.getValue());
        assertEquals(2, SendFlag.SNDMORE.getValue());
        assertEquals(3, SendFlag.DONTWAIT_SNDMORE.getValue());

        assertEquals(0, ReceiveFlag.NONE.getValue());
        assertEquals(1, ReceiveFlag.DONTWAIT.getValue());
    }

    @Test
    public void testMonitorEventAndPollValues() {
        assertEquals(0x0001, MonitorEventType.CONNECTED.getValue());
        assertEquals(0x0200, MonitorEventType.DISCONNECTED.getValue());
        assertEquals(0xFFFF, MonitorEventType.ALL.getValue());

        assertEquals(1, PollEventType.POLLIN.getValue());
        assertEquals(2, PollEventType.POLLOUT.getValue());
        assertEquals(4, PollEventType.POLLERR.getValue());
        assertEquals(8, PollEventType.POLLPRI.getValue());
    }

    @Test
    public void testErrorAndProtocolValues() {
        assertEquals(156384763, ErrorCode.EFSM.getValue());
        assertEquals(156384764, ErrorCode.ENOCOMPATPROTO.getValue());
        assertEquals(156384765, ErrorCode.ETERM.getValue());
        assertEquals(156384766, ErrorCode.EMTHREAD.getValue());

        assertEquals(0x10000013,
            ProtocolError.ZMP_MALFORMED_COMMAND_HELLO.getValue());
    }

    @Test
    public void testServiceAndRoleValues() {
        assertEquals(1, ServiceType.GATEWAY.getValue());
        assertEquals(2, ServiceType.SPOT.getValue());

        assertEquals(0, GatewayLbStrategy.ROUND_ROBIN.getValue());
        assertEquals(1, GatewayLbStrategy.WEIGHTED.getValue());

        assertEquals(1, RegistrySocketRole.PUB.getValue());
        assertEquals(2, RegistrySocketRole.ROUTER.getValue());
        assertEquals(3, RegistrySocketRole.PEER_SUB.getValue());

        assertEquals(1, DiscoverySocketRole.SUB.getValue());

        assertEquals(1, GatewaySocketRole.ROUTER.getValue());

        assertEquals(1, ReceiverSocketRole.ROUTER.getValue());
        assertEquals(2, ReceiverSocketRole.DEALER.getValue());

        assertEquals(0, SpotNodeSocketRole.NODE.getValue());
        assertEquals(1, SpotNodeSocketRole.PUB.getValue());
        assertEquals(2, SpotNodeSocketRole.SUB.getValue());
        assertEquals(3, SpotNodeSocketRole.DEALER.getValue());

        assertEquals(1, SpotNodeOption.PUB_MODE.getValue());
        assertEquals(2, SpotNodeOption.PUB_QUEUE_HWM.getValue());
        assertEquals(3, SpotNodeOption.PUB_QUEUE_FULL_POLICY.getValue());

        assertEquals(0, SpotNodePubMode.SYNC.getValue());
        assertEquals(1, SpotNodePubMode.ASYNC.getValue());

        assertEquals(0, SpotNodePubQueueFullPolicy.EAGAIN.getValue());
        assertEquals(1, SpotNodePubQueueFullPolicy.DROP.getValue());

        assertEquals(1, SpotSocketRole.PUB.getValue());
        assertEquals(2, SpotSocketRole.SUB.getValue());
    }

    @Test
    public void testCombineHelpers() {
        assertEquals(SendFlag.DONTWAIT_SNDMORE,
            SendFlag.combine(SendFlag.DONTWAIT, SendFlag.SNDMORE));
        assertEquals(0x0201, MonitorEventType.combine(
            MonitorEventType.CONNECTED, MonitorEventType.DISCONNECTED));
        assertEquals(3, PollEventType.combine(PollEventType.POLLIN,
            PollEventType.POLLOUT));
    }
}
