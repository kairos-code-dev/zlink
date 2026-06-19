package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

import static org.junit.jupiter.api.Assertions.assertEquals;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.Test;

final class BingoRoomDirectoryTest {
    @Test
    void allocateUsesSameSequenceForSettingsAndRoomId() {
        CapturingMatchQueue queue = new CapturingMatchQueue();
        BingoRoomDirectory directory = new BingoRoomDirectory(null, new ObjectMapper(), queue);

        BingoMatchReservation reservation =
            directory.allocate("player-1", "two-player", "remote-play-node");

        assertEquals("two-player-room-1", reservation.roomId());
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
            String preferredOwnerNodeRid,
            String newRoomId,
            int requiredPlayers) {
            this.newRoomId = newRoomId;
            this.requiredPlayers = requiredPlayers;
            return new BingoMatchReservation(newRoomId, "remote-play-node");
        }
    }
}
