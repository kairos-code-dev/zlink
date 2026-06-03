package systems.zlink.samples.tictactoe.server.play.entryspot.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;

@ZLinkHandlerGroup(SampleNames.PlayActor)
public final class PlayActorJoinGameHandler {
    @ZLinkSpotActorRequest
    public CompletionStage<JoinGameRes> joinGame(
        PlayActor actor,
        JoinGameReq request) {
        return actor.context()
            .joinSpot(RoutingId.fromHex(request.gameId()), request)
            .submitAsync(JoinGameRes.class)
            .thenApply(result -> {
                actor.joinGame(request.gameId());
                return result.reply();
            });
    }
}
