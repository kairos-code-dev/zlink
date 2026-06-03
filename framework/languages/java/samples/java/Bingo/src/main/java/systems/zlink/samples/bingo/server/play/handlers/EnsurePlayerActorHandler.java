package systems.zlink.samples.bingo.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class EnsurePlayerActorHandler
    implements ZLinkRequestHandler<Messages.EnsurePlayerActorReq, Messages.EnsurePlayerActorRes> {
    @Override
    public CompletionStage<Messages.EnsurePlayerActorRes> handleAsync(
        Messages.EnsurePlayerActorReq request,
        ZLinkRequestContext context) {
        return CompletableFuture.completedFuture(new Messages.EnsurePlayerActorRes(
            request.actorId(),
            SampleNames.PlayerActorType));
    }
}
