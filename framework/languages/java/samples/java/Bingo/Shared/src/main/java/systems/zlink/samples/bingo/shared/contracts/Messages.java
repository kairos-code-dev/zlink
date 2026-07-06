package systems.zlink.samples.bingo.shared.contracts;

import java.util.List;
import systems.zlink.framework.handlers.ZLinkPacket;

public final class Messages {
    private Messages() {
    }

    @ZLinkPacket("AuthenticateReq")
    public record AuthenticateReq(String accessToken) {
    }

    public record AuthenticateRes(String actorId, String displayName, String actorNodeRid) {
    }

    @ZLinkPacket("AuthenticatePlayer")
    public record AuthenticatePlayerReq(String accessToken) {
    }

    public record AuthenticatePlayerRes(boolean accepted, String actorId, String displayName, String reason) {
    }

    @ZLinkPacket("EnsurePlayerActor")
    public record EnsurePlayerActorReq(String actorId, String displayName, String preferredActorNodeRid) {
    }

    public record ActorRefWire(String nodeRid, String actorId, long generation) {
    }

    public record EnsurePlayerActorRes(String actorId, String actorType, ActorRefWire actor) {
    }

    @ZLinkPacket("MatchBingoReq")
    public record MatchBingoReq(String mode) {
    }

    public record MatchBingoRes(String roomId, BingoRoomState state, String roomOwnerNodeRid) {
    }

    @ZLinkPacket("MatchBingoApiReq")
    public record MatchBingoApiReq(String actorId, String displayName, String mode, String actorNodeRid) {
    }

    public record MatchBingoApiRes(String roomId, String roomOwnerNodeRid) {
    }

    @ZLinkPacket("AllocateBingoRoomReq")
    public record AllocateBingoRoomReq(String actorId, String mode, String preferredOwnerNodeRid) {
    }

    public record AllocateBingoRoomRes(String roomId, String roomOwnerNodeRid) {
    }

    @ZLinkPacket("BingoRoomJoinReq")
    public record BingoRoomJoinReq(String roomId, String actorId, String displayName, boolean observeOnly) {
    }

    public record BingoRoomJoinRes(BingoRoomState state) {
    }

    @ZLinkPacket("SubmitBingoCardReq")
    public record SubmitBingoCardReq(String roomId, List<Integer> card) {
    }

    public record SubmitBingoCardRes(BingoRoomState state) {
    }

    public record PlayerJoinedNotify(
        String roomId,
        String actorId,
        String displayName,
        int seat,
        boolean isHost,
        BingoRoomState state) {
    }

    public record BingoGameStartedNotify(BingoRoomState state) {
    }

    public record BingoNumberDrawnNotify(
        String roomId,
        int drawSeq,
        int number,
        BingoRoomState state) {
    }

    public record BingoStateNotify(BingoRoomState state) {
    }

    public record BingoGameEndedNotify(BingoRoomState state) {
    }

    @ZLinkPacket("ObserveBingoEventsReq")
    public record ObserveBingoEventsReq(String roomId) {
    }

    public record ObserveBingoEventsRes(boolean subscribed, String observerNodeRid) {
    }

    @ZLinkPacket("StopObservingBingoEventsReq")
    public record StopObservingBingoEventsReq(String roomId) {
    }

    public record StopObservingBingoEventsRes(boolean stopped, String observerNodeRid) {
    }

    public record BingoWinnerMsg(
        String roomId,
        String actorId,
        int drawSeq,
        String itemId,
        String itemName,
        String rarity) {
    }

    public record BingoRewardAnnouncedNotify(
        String roomId,
        String actorId,
        int drawSeq,
        String itemId,
        String itemName,
        String rarity,
        String receivingSpotNodeRid) {
    }

    public record BingoWinner(String actorId) {
    }

    public record BingoRoomState(
        String roomId,
        String status,
        String hostActorId,
        boolean canStart,
        int drawSeq,
        Integer lastDrawnNumber,
        List<Integer> drawnNumbers,
        List<BingoPlayerState> players,
        List<String> winners) {
    }

    public record BingoPlayerState(
        String actorId,
        String displayName,
        int seat,
        boolean isHost,
        List<Integer> card,
        List<Boolean> marks,
        int completedLines) {
    }
}
