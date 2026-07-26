package systems.zlink.samples.bingo.server.play.application.roomallocation;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

final class BingoRoomAllocatorTest {
    @Test
    void allocateUsesSameSequenceForSettingsAndRoomId() {
        CapturingMatchQueue queue = new CapturingMatchQueue();
        BingoRoomAllocator allocator = new BingoRoomAllocator(queue, 100);

        BingoRoomAllocation allocation =
            allocator.allocate("player-1", "two-player");

        assertEquals("two-player-room-1", allocation.roomId());
        assertEquals("Bingo Room 001", allocation.settings().roomName());
        assertEquals(100, allocation.settings().drawPeriodMillis());
        assertEquals("two-player-room-1", queue.newRoomId);
        assertEquals(2, queue.requiredPlayers);
    }

    private static final class CapturingMatchQueue implements BingoMatchQueue {
        private String newRoomId;
        private int requiredPlayers;

        @Override
        public BingoMatchReservation reserve(
            String mode,
            String actorId,
            String newRoomId,
            int requiredPlayers) {
            this.newRoomId = newRoomId;
            this.requiredPlayers = requiredPlayers;
            return new BingoMatchReservation(newRoomId);
        }
    }
}
