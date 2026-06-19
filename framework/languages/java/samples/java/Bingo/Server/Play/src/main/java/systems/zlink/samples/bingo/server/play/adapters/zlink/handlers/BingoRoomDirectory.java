package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.framework.spots.ZLinkSpotCreateState;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoRoomSpot;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;

public final class BingoRoomDirectory {
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;
    private final BingoMatchQueue matchQueue;
    private int roomSeq;

    public BingoRoomDirectory(
        ZLinkSpotManager spots,
        ObjectMapper json,
        BingoMatchQueue matchQueue) {
        this.spots = spots;
        this.json = json;
        this.matchQueue = matchQueue;
    }

    public BingoMatchReservation allocate(String actorId, String mode, String preferredOwnerNodeRid) {
        if (actorId == null || actorId.isBlank()) {
            throw new IllegalStateException("actorId is required");
        }
        if (!"two-player".equals(mode)) {
            throw new IllegalStateException("Unsupported bingo mode. mode=" + mode);
        }

        int roomSeq = nextRoomSeq();
        BingoRoomModels.BingoRoomSettings settings =
            BingoRoomModels.BingoRoomSettings.create(mode, roomSeq);
        BingoMatchReservation reservation = matchQueue.reserve(
            mode,
            actorId,
            preferredOwnerNodeRid,
            settings.mode() + "-room-" + roomSeq,
            settings.requiredPlayers());
        if (!reservation.ownerPlayNodeRid().equals(SampleTopology.selectedPlayNodeRid())) {
            System.out.println(
                "bingo room allocation: remote owner. room=" + reservation.roomId()
                    + ", owner=" + reservation.ownerPlayNodeRid()
                    + ", local=" + SampleTopology.selectedPlayNodeRid());
            return reservation;
        }

        Message settingsPart = serialize(settings);
        try {
            var created = await(spots.getOrCreate(BingoRoomSpot.class, RoutingId.from(reservation.roomId()), settingsPart));
            if (created.state() == ZLinkSpotCreateState.REJECTED) {
                throw new IllegalStateException("Bingo room creation was rejected. room=" + reservation.roomId());
            }
            System.out.println(
                "bingo room allocation: local owner. room=" + reservation.roomId()
                    + ", owner=" + reservation.ownerPlayNodeRid()
                    + ", local=" + SampleTopology.selectedPlayNodeRid()
                    + ", state=" + created.state());
            return reservation;
        } finally {
            settingsPart.close();
        }
    }

    private synchronized int nextRoomSeq() {
        roomSeq += 1;
        return roomSeq;
    }

    private Message serialize(BingoRoomModels.BingoRoomSettings settings) {
        try {
            return Message.from(json.writeValueAsBytes(settings));
        } catch (JsonProcessingException ex) {
            throw new IllegalStateException("Failed to encode bingo room settings.", ex);
        }
    }
}
