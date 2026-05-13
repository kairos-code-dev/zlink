package systems.zlink.contract;

import systems.zlink.Context;
import systems.zlink.DealerSocket;
import systems.zlink.Message;
import systems.zlink.Received;
import systems.zlink.RouterSocket;
import systems.zlink.RoutingId;
import systems.zlink.SubmitException;
import systems.zlink.SubmitResult;
import systems.zlink.TestSupport;
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

            dealer.send()
                .message(Message.copyOfUtf8("part-1"))
                .message(Message.copyOfUtf8("part-2"))
                .submit();

            try (systems.zlink.Received inbound = new systems.zlink.Received()) {


                router.recv(inbound, systems.zlink.RecvFlags.NONE);
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
                    () -> inbound.reply()
                        .message(Message.copyOfUtf8("ack"))
                        .submit());
                assertEquals(SubmitResult.INVALID_STATE, ex.getResult());
            }
        }
    }
}
