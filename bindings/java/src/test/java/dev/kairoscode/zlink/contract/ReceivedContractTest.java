package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
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

        RoutingId dealerRid = RoutingId.fromBytes("dealer-a".getBytes(StandardCharsets.UTF_8));
        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             DealerSocket dealer = new DealerSocket(ctx)) {
            dealer.setRoutingId(dealerRid);
            String endpoint = TestSupport.inprocEndpoint("received-contract");
            router.bind(endpoint);
            dealer.connect(endpoint);

            dealer.send(List.of(Message.copyOfUtf8("part-1"),
                Message.copyOfUtf8("part-2")));

            try (Received inbound = router.recv()) {
                assertTrue(inbound.routingId().isPresent());
                assertArrayEquals(dealerRid.toBytes(),
                    inbound.routingId().orElseThrow().toBytes());
                assertEquals(2, inbound.parts().size());
                assertFalse(inbound.isSinglePart());
                assertThrows(UnsupportedOperationException.class,
                    () -> inbound.parts().add(Message.copyOfUtf8("x")));
                assertArrayEquals("part-1".getBytes(StandardCharsets.UTF_8),
                    inbound.firstPart().toByteArray());
                assertTrue(inbound.requestSeq().isEmpty());
                SubmitException ex = assertThrows(SubmitException.class,
                    () -> inbound.reply(List.of(Message.copyOfUtf8("ack"))));
                assertEquals(SubmitResult.INVALID_STATE, ex.getResult());
            }
        }
    }
}
