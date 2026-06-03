package systems.zlink.samples.bingo.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play")
public final class EnsurePlayerActorHandler {
    @ZLinkRequest(packetName = "EnsurePlayerActor")
    public CompletionStage<Messages.EnsurePlayerActorRes> handleAsync(
        Messages.EnsurePlayerActorReq request) {
        return CompletableFuture.completedFuture(new Messages.EnsurePlayerActorRes(
            request.actorId(),
            SampleNames.PlayerActorType));
    }
}
