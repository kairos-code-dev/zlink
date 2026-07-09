package systems.zlink.samples.bingo.shared.contracts;

import java.util.List;

public final class BingoMessages {
    private BingoMessages() {
    }

    public static Messages.AuthenticateReq authenticateReq(String accessToken) {
        return Messages.AuthenticateReq.newBuilder()
            .setAccessToken(accessToken)
            .build();
    }

    public static Messages.AuthenticateRes authenticateRes(
        String actorId,
        String displayName,
        String actorNodeRid) {
        return Messages.AuthenticateRes.newBuilder()
            .setActorId(actorId)
            .setDisplayName(displayName)
            .setActorNodeRid(actorNodeRid)
            .build();
    }

    public static Messages.AuthenticatePlayerReq authenticatePlayerReq(String accessToken) {
        return Messages.AuthenticatePlayerReq.newBuilder()
            .setAccessToken(accessToken)
            .build();
    }

    public static Messages.AuthenticatePlayerRes authenticatePlayerRes(
        boolean accepted,
        String actorId,
        String displayName,
        String reason) {
        Messages.AuthenticatePlayerRes.Builder builder = Messages.AuthenticatePlayerRes.newBuilder()
            .setAccepted(accepted)
            .setActorId(actorId == null ? "" : actorId)
            .setDisplayName(displayName == null ? "" : displayName);
        if (reason != null) {
            builder.setReason(reason);
        }
        return builder.build();
    }

    public static Messages.EnsurePlayerActorReq ensurePlayerActorReq(
        String actorId,
        String displayName,
        String preferredActorNodeRid) {
        return Messages.EnsurePlayerActorReq.newBuilder()
            .setActorId(actorId)
            .setDisplayName(displayName)
            .setPreferredActorNodeRid(preferredActorNodeRid)
            .build();
    }

    public static Messages.ActorRefWire actorRefWire(String nodeRid, String actorId, long generation) {
        return Messages.ActorRefWire.newBuilder()
            .setNodeRid(nodeRid)
            .setActorId(actorId)
            .setGeneration(generation)
            .build();
    }

    public static Messages.EnsurePlayerActorRes ensurePlayerActorRes(
        String actorId,
        String actorType,
        Messages.ActorRefWire actor) {
        return Messages.EnsurePlayerActorRes.newBuilder()
            .setActorId(actorId)
            .setActorType(actorType)
            .setActor(actor)
            .build();
    }

    public static Messages.MatchBingoReq matchBingoReq(String mode) {
        return Messages.MatchBingoReq.newBuilder()
            .setMode(mode)
            .build();
    }

    public static Messages.MatchBingoRes matchBingoRes(
        String roomId,
        Messages.BingoRoomState state,
        String roomOwnerNodeRid) {
        return Messages.MatchBingoRes.newBuilder()
            .setRoomId(roomId)
            .setState(state)
            .setRoomOwnerNodeRid(roomOwnerNodeRid)
            .build();
    }

    public static Messages.MatchBingoApiReq matchBingoApiReq(
        String actorId,
        String displayName,
        String mode,
        String actorNodeRid) {
        return Messages.MatchBingoApiReq.newBuilder()
            .setActorId(actorId)
            .setDisplayName(displayName)
            .setMode(mode)
            .setActorNodeRid(actorNodeRid)
            .build();
    }

    public static Messages.MatchBingoApiRes matchBingoApiRes(String roomId, String roomOwnerNodeRid) {
        return Messages.MatchBingoApiRes.newBuilder()
            .setRoomId(roomId)
            .setRoomOwnerNodeRid(roomOwnerNodeRid)
            .build();
    }

    public static Messages.AllocateBingoRoomReq allocateBingoRoomReq(
        String mode,
        String actorId,
        String preferredOwnerNodeRid) {
        return Messages.AllocateBingoRoomReq.newBuilder()
            .setMode(mode)
            .setActorId(actorId)
            .setPreferredOwnerNodeRid(preferredOwnerNodeRid)
            .build();
    }

    public static Messages.AllocateBingoRoomRes allocateBingoRoomRes(String roomId, String roomOwnerNodeRid) {
        return Messages.AllocateBingoRoomRes.newBuilder()
            .setRoomId(roomId)
            .setRoomOwnerNodeRid(roomOwnerNodeRid)
            .build();
    }

    public static Messages.BingoRoomJoinReq bingoRoomJoinReq(
        String roomId,
        String actorId,
        String displayName,
        boolean observeOnly) {
        return Messages.BingoRoomJoinReq.newBuilder()
            .setRoomId(roomId)
            .setActorId(actorId)
            .setDisplayName(displayName)
            .setObserveOnly(observeOnly)
            .build();
    }

    public static Messages.BingoRoomJoinRes bingoRoomJoinRes(Messages.BingoRoomState state) {
        return Messages.BingoRoomJoinRes.newBuilder()
            .setState(state)
            .build();
    }

    public static Messages.SubmitBingoCardReq submitBingoCardReq(String roomId, List<Integer> card) {
        return Messages.SubmitBingoCardReq.newBuilder()
            .setRoomId(roomId)
            .addAllCard(card)
            .build();
    }

    public static Messages.SubmitBingoCardRes submitBingoCardRes(Messages.BingoRoomState state) {
        return Messages.SubmitBingoCardRes.newBuilder()
            .setState(state)
            .build();
    }

    public static Messages.ObserveBingoEventsReq observeBingoEventsReq(String roomId) {
        return Messages.ObserveBingoEventsReq.newBuilder()
            .setRoomId(roomId)
            .build();
    }

    public static Messages.ObserveBingoEventsRes observeBingoEventsRes(boolean subscribed, String observerNodeRid) {
        return Messages.ObserveBingoEventsRes.newBuilder()
            .setSubscribed(subscribed)
            .setObserverNodeRid(observerNodeRid)
            .build();
    }

    public static Messages.StopObservingBingoEventsReq stopObservingBingoEventsReq(String roomId) {
        return Messages.StopObservingBingoEventsReq.newBuilder()
            .setRoomId(roomId)
            .build();
    }

    public static Messages.StopObservingBingoEventsRes stopObservingBingoEventsRes(
        boolean stopped,
        String observerNodeRid) {
        return Messages.StopObservingBingoEventsRes.newBuilder()
            .setStopped(stopped)
            .setObserverNodeRid(observerNodeRid)
            .build();
    }

    public static Messages.PlayerJoinedNotify playerJoinedNotify(
        String roomId,
        String actorId,
        String displayName,
        int seat,
        boolean host,
        Messages.BingoRoomState state) {
        return Messages.PlayerJoinedNotify.newBuilder()
            .setRoomId(roomId)
            .setActorId(actorId)
            .setDisplayName(displayName)
            .setSeat(seat)
            .setIsHost(host)
            .setState(state)
            .build();
    }

    public static Messages.BingoGameStartedNotify bingoGameStartedNotify(Messages.BingoRoomState state) {
        return Messages.BingoGameStartedNotify.newBuilder()
            .setState(state)
            .build();
    }

    public static Messages.BingoNumberDrawnNotify bingoNumberDrawnNotify(
        String roomId,
        int drawSeq,
        int number,
        Messages.BingoRoomState state) {
        return Messages.BingoNumberDrawnNotify.newBuilder()
            .setRoomId(roomId)
            .setDrawSeq(drawSeq)
            .setNumber(number)
            .setState(state)
            .build();
    }

    public static Messages.BingoStateNotify bingoStateNotify(Messages.BingoRoomState state) {
        return Messages.BingoStateNotify.newBuilder()
            .setState(state)
            .build();
    }

    public static Messages.BingoGameEndedNotify bingoGameEndedNotify(Messages.BingoRoomState state) {
        return Messages.BingoGameEndedNotify.newBuilder()
            .setState(state)
            .build();
    }

    public static Messages.BingoRewardAnnouncedNotify bingoRewardAnnouncedNotify(
        String roomId,
        String actorId,
        int drawSeq,
        String itemId,
        String itemName,
        String rarity,
        String receivingSpotNodeRid) {
        return Messages.BingoRewardAnnouncedNotify.newBuilder()
            .setRoomId(roomId)
            .setActorId(actorId)
            .setDrawSeq(drawSeq)
            .setItemId(itemId)
            .setItemName(itemName)
            .setRarity(rarity)
            .setReceivingSpotNodeRid(receivingSpotNodeRid)
            .build();
    }

    public static Messages.BingoWinnerMsg bingoWinnerMsg(
        String roomId,
        String actorId,
        int drawSeq,
        String itemId,
        String itemName,
        String rarity) {
        return Messages.BingoWinnerMsg.newBuilder()
            .setRoomId(roomId)
            .setActorId(actorId)
            .setDrawSeq(drawSeq)
            .setItemId(itemId)
            .setItemName(itemName)
            .setRarity(rarity)
            .build();
    }

    public static Messages.BingoRoomState bingoRoomState(
        String roomId,
        String status,
        String hostActorId,
        boolean canStart,
        int drawSeq,
        Integer lastDrawnNumber,
        List<Integer> drawnNumbers,
        List<Messages.BingoPlayerState> players,
        List<String> winners) {
        Messages.BingoRoomState.Builder builder = Messages.BingoRoomState.newBuilder()
            .setRoomId(roomId)
            .setStatus(status)
            .setHostActorId(hostActorId)
            .setCanStart(canStart)
            .setDrawSeq(drawSeq)
            .addAllDrawnNumbers(drawnNumbers)
            .addAllPlayers(players)
            .addAllWinners(winners);
        if (lastDrawnNumber != null) {
            builder.setLastDrawnNumber(lastDrawnNumber);
        }
        return builder.build();
    }

    public static Messages.BingoPlayerState bingoPlayerState(
        String actorId,
        String displayName,
        int seat,
        boolean host,
        List<Integer> card,
        List<Boolean> marks,
        int completedLines) {
        return Messages.BingoPlayerState.newBuilder()
            .setActorId(actorId)
            .setDisplayName(displayName)
            .setSeat(seat)
            .setIsHost(host)
            .addAllCard(card)
            .addAllMarks(marks)
            .setCompletedLines(completedLines)
            .build();
    }
}
