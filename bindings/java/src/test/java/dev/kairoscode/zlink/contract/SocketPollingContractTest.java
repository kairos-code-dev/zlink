package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEvent;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.TestSupport;
import java.util.List;
import java.lang.reflect.Modifier;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SocketPollingContractTest {
    @Test
    public void pollerTracksTypedSocketsThroughAbstractBase() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx);
             Poller poller = new Poller()) {
            String endpoint = TestSupport.inprocEndpoint("poller-contract");
            server.bind(endpoint);
            client.connect(endpoint);
            poller.add(server, PollEventType.POLLIN);

            try (Message outbound = Message.copyOfUtf8("poller")) {
                client.send(outbound);
            }

            assertTrue(poller.pollAny(TestSupport.DEFAULT_TIMEOUT_MS));
            assertEquals(1, poller.size());
            assertSame(server, poller.readySocket(0));
            assertEquals(PollEventType.POLLIN.getValue(),
                poller.readyEvents(0));
            List<PollEvent> events = poller.poll(0);
            assertEquals(1, events.size());
            assertSame(server, events.get(0).socket());
        }
    }

    @Test
    public void legacySocketPollSetTypeIsHidden() throws Exception {
        assertFalse(Modifier.isPublic(
            Class.forName("dev.kairoscode.zlink.SocketPollSet")
              .getModifiers()));
    }
}
