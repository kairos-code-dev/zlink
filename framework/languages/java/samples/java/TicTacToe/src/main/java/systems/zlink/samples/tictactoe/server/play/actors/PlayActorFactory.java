package systems.zlink.samples.tictactoe.server.play.actors;

public final class PlayActorFactory {
    public PlayActor create(String actorId) {
        return new PlayActor(actorId);
    }
}
