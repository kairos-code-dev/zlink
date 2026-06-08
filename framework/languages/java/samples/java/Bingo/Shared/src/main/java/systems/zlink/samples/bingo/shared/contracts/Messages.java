package systems.zlink.samples.bingo.shared.contracts;

import java.util.List;
import systems.zlink.framework.handlers.ZLinkPacket;

public final class Messages {
    private Messages() {
    }

    public record AuthenticateReq(String accessToken) {
    }

    public record AuthenticateRes(String actorId, String displayName) {
    }

    @ZLinkPacket("AuthenticatePlayer")
    public record AuthenticatePlayerReq(String accessToken) {
    }

    public record AuthenticatePlayerRes(boolean accepted, String actorId, String displayName, String reason) {
    }

    @ZLinkPacket("EnsurePlayerActor")
    public record EnsurePlayerActorReq(String actorId, String displayName) {
    }

    public record ActorRefSnapshot(byte[] nodeRid, String actorId, long generation) {
    }

    public record EnsurePlayerActorRes(String actorId, String actorType, ActorRefSnapshot actor) {
    }

    @ZLinkPacket("MatchBingoReq")
    public record MatchBingoReq(String mode) {
    }

    public record MatchBingoRes(String roomId, BingoRoomState state) {
    }

    @ZLinkPacket("MatchBingoApiReq")
    public record MatchBingoApiReq(String actorId, String displayName, String mode) {
    }

    public record MatchBingoApiRes(String roomId) {
    }

    @ZLinkPacket("AllocateBingoRoomReq")
    public record AllocateBingoRoomReq(String actorId, String mode) {
    }

    public record AllocateBingoRoomRes(String roomId) {
    }

    @ZLinkPacket("BingoRoomJoinReq")
    public record BingoRoomJoinReq(String roomId, String actorId, String displayName) {
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
