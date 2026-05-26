package systems.zlink.contract;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.DealerSocket;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.Received;
import systems.zlink.contracts.RouterSocket;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SubmitException;
import systems.zlink.contracts.SubmitResult;
import systems.zlink.contracts.TestSupport;
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
                .message(Message.from("part-1"))
                .message(Message.from("part-2"))
                .submit();

            try (systems.zlink.contracts.Received inbound = new systems.zlink.contracts.Received()) {


                router.recv(inbound, systems.zlink.contracts.RecvFlags.NONE);
                assertTrue(inbound.routingId().isPresent());
                assertArrayEquals(dealerRid.toBytes(),
                    inbound.routingId().orElseThrow().toBytes());
                assertEquals(2, inbound.parts().size());
                assertFalse(inbound.isSinglePart());
                assertThrows(UnsupportedOperationException.class,
                    () -> inbound.parts().add(Message.from("x")));
                assertArrayEquals("part-1".getBytes(StandardCharsets.UTF_8),
                    inbound.firstPart().toByteArray());
                assertTrue(inbound.requestSeq().isEmpty());
                SubmitException ex = assertThrows(SubmitException.class,
                    () -> inbound.reply()
                        .message(Message.from("ack"))
                        .submit());
                assertEquals(SubmitResult.INVALID_STATE, ex.getResult());
            }
        }
    }
}
