package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRefSnapshot;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play-route")
public final class EnsurePlayerActorHandler
    implements ZLinkRouteRequestHandler<
        Messages.EnsurePlayerActorReq,
        Messages.EnsurePlayerActorRes> {
    private final ZLinkActorManager actors;

    public EnsurePlayerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.EnsurePlayerActorRes handle(
        Messages.EnsurePlayerActorReq request,
        ZLinkRouteRequestContext context) {
        if (request.preferredActorNodeRid() != null
            && !request.preferredActorNodeRid().isBlank()
            && !request.preferredActorNodeRid().equals(SampleTopology.selectedPlayNodeRid())) {
            throw new IllegalStateException("EnsurePlayerActor reached the wrong Play node.");
        }
        var actor = await(actors.getOrCreate(
            request.actorId(),
            SampleNames.PlayerActorType,
            request));
        return new Messages.EnsurePlayerActorRes(
            request.actorId(),
            SampleNames.PlayerActorType,
            ZLinkActorRefSnapshot.from(actor));
    }
}
