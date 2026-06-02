package systems.zlink.samples.bingo.server.play.actors;

public final class PlayerActorFactory {
    public PlayerActor create(String actorId) {
        return new PlayerActor(actorId);
    }
}
