package systems.zlink.samples.tictactoe.sessiongateway.client;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.PlayerSession;
import systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.RecordingSessionActors;

public final class SessionActorDispatchPlayerClient {
    private final RecordingSessionActors actors = new RecordingSessionActors();
    private final PlayerSession session = new PlayerSession(actors);

    CompletionStage<?> bindAsync(ZLinkActorRef actorRef) {
        return session.context().actors().bindAsync(actorRef);
    }

    String boundActorId() {
        return actors.bound().get(0).actorId();
    }
}
