package systems.zlink.samples.tictactoe.server.play.actors;

public final class PlayActor {
    private final String actorId;
    private String gameId = "";

    public PlayActor(String actorId) {
        this.actorId = actorId;
    }

    public String actorId() {
        return actorId;
    }

    public String gameId() {
        return gameId;
    }

    public void joinGame(String gameId) {
        this.gameId = gameId;
    }
}
