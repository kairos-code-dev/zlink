package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.TestSupport;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertSame;
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
            assertSame(server, poller.readySocket(0));
            assertEquals(PollEventType.POLLIN.getValue(),
                poller.readyEvents(0));
        }
    }

    @Test
    public void socketPollSetTracksTypedSocketsThroughAbstractBase() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx);
             SocketPollSet pollSet = SocketPollSet.fromSockets(
                 java.util.List.of(server), PollEventType.POLLIN.getValue())) {
            String endpoint = TestSupport.inprocEndpoint("pollset-contract");
            server.bind(endpoint);
            client.connect(endpoint);

            try (Message outbound = Message.copyOfUtf8("pollset")) {
                client.send(outbound);
            }

            assertEquals(1, pollSet.poll(TestSupport.DEFAULT_TIMEOUT_MS));
            assertSame(server, pollSet.socket(0));
            assertTrue(pollSet.isReady(0, PollEventType.POLLIN.getValue()));
        }
    }
}
