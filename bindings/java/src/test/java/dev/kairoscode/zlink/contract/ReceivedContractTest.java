package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.TestSupport;
import java.nio.charset.StandardCharsets;
import java.util.List;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class ReceivedContractTest {
    @Test
    public void recvReturnsAggregateWithRoutingIdAndMultipartView() {
        TestSupport.assumeNative();

        RoutingId dealerRid = RoutingId.copyOf("dealer-a".getBytes(StandardCharsets.UTF_8));
        try (Context ctx = new Context();
             Socket router = new Socket(ctx, SocketType.ROUTER);
             Socket dealer = new Socket(ctx, SocketType.DEALER)) {
            dealer.setRoutingId(dealerRid);
            String endpoint = TestSupport.inprocEndpoint("received-contract");
            router.bind(endpoint);
            dealer.connect(endpoint);

            dealer.send(List.of(Message.copyOfUtf8("part-1"),
                Message.copyOfUtf8("part-2")));

            try (Received inbound = router.recv(ReceiveFlag.NONE)) {
                assertTrue(inbound.hasRoutingId());
                assertArrayEquals(dealerRid.toByteArray(),
                    inbound.routingId().toByteArray());
                assertEquals(2, inbound.parts().size());
                assertFalse(inbound.isSinglePart());
                assertThrows(UnsupportedOperationException.class,
                    () -> inbound.parts().add(Message.copyOfUtf8("x")));
                assertArrayEquals("part-1".getBytes(StandardCharsets.UTF_8),
                    inbound.firstPart().toByteArray());

                RoutingId replyRid = inbound.routingId();
                router.send(replyRid, List.of(Message.copyOfUtf8("ack")));
            }

            try (Received ack = dealer.recv()) {
                assertTrue(ack.isSinglePart());
                assertArrayEquals("ack".getBytes(StandardCharsets.UTF_8),
                    ack.singlePartOrThrow().toByteArray());
                Message part = ack.singlePartOrThrow();
                ack.close();
                assertFalse(part.valid());
            }
        }
    }
}
