package systems.zlink.samples.bingo.server.play.entryspot.handlers;

public final class BingoEntrySpotActorJoinedHandler {
    public String handle(String actorId) {
        return actorId + " joined bingo entry spot";
    }
}
