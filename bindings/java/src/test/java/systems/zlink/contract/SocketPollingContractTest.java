package systems.zlink.contract;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.PollEvent;
import systems.zlink.PairSocket;
import systems.zlink.PollEventFlag;
import systems.zlink.Poller;
import systems.zlink.TestSupport;
import systems.zlink.Timer;
import java.lang.reflect.Modifier;
import java.time.Duration;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
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
            poller.add(server, PollEventFlag.POLLIN);

            try (Message outbound = Message.copyOfUtf8("poller")) {
                client.send(outbound);
            }

            PollEvent event = poller.wait(
                Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));
            assertNotNull(event);
            assertEquals(1, poller.size());
            assertSame(server, event.socket());
            assertTrue(event.events().contains(PollEventFlag.POLLIN));
            assertTrue(event.revents().contains(PollEventFlag.POLLIN));
        }
    }

    @Test
    public void pollerTracksTimersThroughCoreTimerRegistration() {
        TestSupport.assumeNative();

        try (Timer timer = new Timer();
             Poller poller = new Poller()) {
            Object tag = new Object();
            poller.add(timer, tag);
            timer.start(Duration.ofMillis(1), 1L);

            PollEvent event = poller.wait(
                Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));
            assertNotNull(event);
            assertEquals(1, poller.size());
            assertSame(timer, event.timer());
            assertSame(tag, event.tag());
            assertTrue(timer.recv() > 0L);
        }
    }

    @Test
    public void legacySocketPollSetTypeIsHidden() throws Exception {
        assertFalse(Modifier.isPublic(
            Class.forName("systems.zlink.SocketPollSet")
              .getModifiers()));
    }
}
