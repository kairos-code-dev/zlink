package systems.zlink.samples.tictactoe.sessiongateway.client;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkFramework;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class SessionActorDispatchPlayerClient {
    private final ZLinkSessionActors actors;

    public SessionActorDispatchPlayerClient(
        ZLinkFramework framework,
        RoutingId sessionRid) {
        this.actors = framework.sessionActors(SampleNames.GatewayStream, sessionRid);
    }

    CompletionStage<?> bindAsync(ZLinkActorRef actorRef) {
        return actors.bindAsync(actorRef);
    }

    String boundActorId() {
        return actors.bound().get(0).actorId();
    }
}
