package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;

final class ZLinkActorContextStateRelocationTest {
    @Test
    void snapshotsBoundSessionFenceWithoutAdvancingAcceptedSequence() {
        RoutingId actorNode = RoutingId.from("actor-node");
        RoutingId sessionOwner = RoutingId.from("session-owner");
        RoutingId session = RoutingId.from("session-a");
        var state = new ZLinkActorContextState(
            new ZLinkBackendActorRef(actorNode, "actor-a", 7),
            "mesh",
            "entry-a");

        long binding = state.bindSession(
            new TestBoundSession(), sessionOwner, session);
        var initial = state.boundSessionSourceSnapshot();
        assertEquals(binding, initial.bindingGeneration());
        assertEquals(0, initial.sessionSequence());

        assertEquals(1, state.nextBoundSessionSource().sessionSequence());
        var sealed = state.boundSessionSourceSnapshot();
        assertEquals(sessionOwner, sealed.sourceNodeRid());
        assertEquals(session, sealed.sourceSessionRid());
        assertEquals(binding, sealed.bindingGeneration());
        assertEquals(1, sealed.sessionSequence());
        assertEquals(1, state.boundSessionSourceSnapshot().sessionSequence());
    }

    private static final class TestBoundSession implements ZLinkBoundSession {
        @Override public ZLinkBoundSessionSendCall send(Object message) {
            throw new UnsupportedOperationException();
        }

        @Override public java.util.concurrent.CompletionStage<Void> disconnect() {
            return CompletableFuture.completedFuture(null);
        }
    }
}
