package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.*;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.actors.ZLinkRelocationCancellation;

final class ZLinkStandaloneActorRelocationStagingOwnerTest {
    @Test
    void actorStaysHiddenUntilReplayAndExplicitPublication() {
        FakeBackend backend = new FakeBackend();
        var owner = new ZLinkStandaloneActorRelocationStagingOwner(backend);
        UUID relocationId = UUID.randomUUID();
        var request = request(relocationId, true);
        byte[] root = ZLinkCanonicalActorRelocationEnvelope.encode(
            relocationId,
            "actor-a",
            7,
            11,
            true,
            new byte[] {4, 5},
            List.of());

        var staged = owner.stage(request, root)
            .toCompletableFuture().join();

        assertEquals(List.of("prepare"), backend.operations);
        assertFalse(backend.visible);

        owner.replayHidden(staged).toCompletableFuture().join();
        assertFalse(backend.visible);
        owner.publish(staged);
        assertTrue(backend.visible);
        assertFalse(backend.admitted);
        owner.openAdmission(staged);
        assertTrue(backend.admitted);
        assertEquals(
            List.of("prepare", "publish", "open"),
            backend.operations);
    }

    @Test
    void discardedTargetNeverBecomesVisible() {
        FakeBackend backend = new FakeBackend();
        var owner = new ZLinkStandaloneActorRelocationStagingOwner(backend);
        UUID relocationId = UUID.randomUUID();
        var staged = owner.stage(
                request(relocationId, false),
                ZLinkCanonicalActorRelocationEnvelope.encode(
                    relocationId,
                    "actor-a",
                    7,
                    11,
                    false,
                    new byte[0],
                    List.of()))
            .toCompletableFuture().join();

        owner.discard(staged).toCompletableFuture().join();

        assertFalse(backend.visible);
        assertTrue(backend.discarded);
        assertThrows(IllegalStateException.class, () -> owner.publish(staged));
    }

    @Test
    void rootMustMatchActorAuthorityFence() {
        FakeBackend backend = new FakeBackend();
        var owner = new ZLinkStandaloneActorRelocationStagingOwner(backend);
        UUID relocationId = UUID.randomUUID();
        byte[] root = ZLinkCanonicalActorRelocationEnvelope.encode(
            relocationId,
            "actor-a",
            8,
            11,
            false,
            new byte[0],
            List.of());

        assertThrows(
            IllegalArgumentException.class,
            () -> owner.stage(request(relocationId, true), root));
        assertTrue(backend.operations.isEmpty());
    }

    private static ZLinkStandaloneActorRelocationStagingOwner.Request request(
        UUID relocationId,
        boolean restoreSnapshot) {
        return new ZLinkStandaloneActorRelocationStagingOwner.Request(
            relocationId,
            "actor-a",
            "player",
            7,
            11,
            restoreSnapshot,
            "target-entry");
    }

    private static final class FakeBackend
        implements ZLinkStandaloneActorRelocationStagingOwner.Backend {
        private final List<String> operations = new ArrayList<>();
        private boolean visible;
        private boolean admitted;
        private boolean discarded;

        @Override
        public CompletionStage<Object> prepare(
            ZLinkStandaloneActorRelocationStagingOwner.Request request,
            byte[] state,
            ZLinkRelocationCancellation cancellation) {
            operations.add("prepare");
            assertArrayEquals(
                request.restoreSnapshot() ? new byte[] {4, 5} : new byte[0],
                state);
            assertFalse(cancellation.isCancellationRequested());
            return CompletableFuture.completedFuture("prepared");
        }

        @Override
        public CompletionStage<Optional<byte[]>> replay(
            Object actor,
            ZLinkStandaloneActorRelocationStagingOwner.Request request,
            ZLinkActorAcceptedJournal.Record record) {
            fail("empty journal must not dispatch a record");
            return CompletableFuture.completedFuture(Optional.empty());
        }

        @Override
        public void publish(Object actor) {
            operations.add("publish");
            visible = true;
        }

        @Override
        public void openAdmission(Object actor) {
            operations.add("open");
            admitted = true;
        }

        @Override
        public CompletionStage<Void> discard(Object actor) {
            operations.add("discard");
            discarded = true;
            return CompletableFuture.completedFuture(null);
        }
    }
}
