package systems.zlink.contract;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.PairSocket;
import systems.zlink.contracts.PollEvents;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.PollSourceKind;
import systems.zlink.contracts.Poller;
import systems.zlink.contracts.Socket;
import systems.zlink.contracts.TestSupport;
import systems.zlink.contracts.Timer;
import java.time.Duration;
import java.util.List;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
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
            poller.add(server, 7L, PollEventFlag.POLLIN);

            try (Message outbound = Message.copyOfUtf8("poller")) {
                client.send().message(outbound).submit();
            }

            PollEvents events = new PollEvents(4);
            int count = poller.wait(events,
                Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));
            assertEquals(1, count);
            assertEquals(1, events.readyCount());
            assertEquals(1, poller.size());
            assertEquals(PollSourceKind.SOCKET, events.sourceKind(0));
            assertEquals(7L, events.slot(0));
            assertTrue(events.hasEvent(0, PollEventFlag.POLLIN));
        }
    }

    @Test
    public void pollerTracksTimersThroughCoreTimerRegistration() {
        TestSupport.assumeNative();

        try (Timer timer = new Timer();
             Poller poller = new Poller()) {
            poller.add(timer, 11L);
            timer.start(Duration.ofMillis(1), 1L);

            PollEvents events = new PollEvents(4);
            int count = poller.wait(events,
                Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));
            assertEquals(1, count);
            assertEquals(1, poller.size());
            assertEquals(PollSourceKind.TIMER, events.sourceKind(0));
            assertEquals(11L, events.slot(0));
            assertTrue(timer.recv() > 0L);
        }
    }

    @Test
    public void pollerRejectsEmptyReusableEventBuffer() {
        assertThrows(IllegalArgumentException.class, () -> new PollEvents(0));
    }

    @Test
    public void pollerSurfaceDoesNotExposeAllocationWaitOrObjectTags()
        throws Exception {
        assertFalse(hasPublicMethod(Poller.class, "wait", Duration.class));
        assertFalse(hasPublicMethod(Poller.class, "wait", int.class,
            Duration.class));
        assertFalse(hasPublicMethod(Poller.class, "wait", List.class,
            Duration.class));
        assertFalse(hasPublicMethod(Poller.class, "add", Timer.class,
            Object.class));
        assertFalse(hasPublicMethod(Poller.class, "add", Socket.class,
            Object.class, PollEventFlag[].class));
    }

    @Test
    public void legacySocketPollSetTypeIsNotPartOfTheRootPackage() {
        assertThrows(
            ClassNotFoundException.class,
            () -> Class.forName("systems.zlink.contracts.SocketPollSet"));
    }

    private static boolean hasPublicMethod(Class<?> type, String name,
                                           Class<?>... parameters) {
        try {
            type.getMethod(name, parameters);
            return true;
        } catch (NoSuchMethodException ex) {
            return false;
        }
    }
}
