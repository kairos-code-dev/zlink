package systems.zlink.samples.tictactoe.sessiongateway.client;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActor;

public final class SessionActorDispatchClient {
    private final SessionActorNotificationInbox inbox = new SessionActorNotificationInbox();

    public void verifyFrameworkActorCreation(ZLinkActorManager actorManager) throws Exception {
        ZLinkActor created = awaitSample(actorManager.createAsync("player-1", "player"));
        require(created.actorId().equals("player-1"),
            "framework actor manager did not create expected actor");
    }

    public void runReconnectScenario(SessionActorDispatchClientOptions options) throws Exception {
        ZLinkActorRef actorRef = new ZLinkActorRef(RoutingId.from("play-node"), options.actorId(), 1);
        SessionActorDispatchPlayerClient primary = new SessionActorDispatchPlayerClient();
        awaitSample(primary.bindAsync(actorRef));

        SessionActorDispatchPlayerClient reconnect = new SessionActorDispatchPlayerClient();
        awaitSample(reconnect.bindAsync(actorRef));

        require(primary.boundActorId().equals(options.actorId()),
            "primary session did not bind expected actor");
        require(reconnect.boundActorId().equals(options.actorId()),
            "reconnect session did not keep actor id");

        PlayerActor actor = new PlayerActor(options.actorId());
        awaitSample(actor.context().boundSession()
            .send("GameState")
            .packetName("GameStateChanged")
            .submitAsync());
        inbox.addAll(actor.pushes());
        require(inbox.events().contains("GameStateChanged:GameState"),
            "boundSession push did not reach client-facing session");
    }

    private static <T> T awaitSample(CompletionStage<T> stage) throws Exception {
        CompletableFuture<T> done = new CompletableFuture<>();
        stage.whenComplete((value, error) -> {
            if (error != null) {
                done.completeExceptionally(error);
            } else {
                done.complete(value);
            }
        });
        return done.get();
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
