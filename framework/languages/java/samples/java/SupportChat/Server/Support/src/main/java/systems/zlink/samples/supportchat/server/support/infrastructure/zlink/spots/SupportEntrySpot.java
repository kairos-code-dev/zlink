package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class SupportEntrySpot implements ZLinkEntrySpot<SupportUserActor> {
    private final ZLinkEntrySpotContext context;
    private final ZLinkActorManager actors;
    private final SupportActorDirectory directory;

    public SupportEntrySpot(
        ZLinkEntrySpotContext context,
        ZLinkActorManager actors,
        SupportActorDirectory directory) {
        this.context = context;
        this.actors = actors;
        this.directory = directory;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void configure() {
    }

    @Override
    public void onCreateActor(
        SupportUserActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken) {
        Messages.EnsureSupportUserActorReq request =
            createRequest.decode(Messages.EnsureSupportUserActorReq.class);
        actor.setIdentity(request.displayName(), request.role());
        var ref = await(actors.find(actor.actorId()))
            .orElseThrow(() -> new IllegalStateException("Support actor ref is not available."));
        directory.addOrUpdate(ref, actor.displayName(), actor.role());
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        SupportUserActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }

    @Override
    public void onJoinedActor(SupportUserActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(SupportUserActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onDisconnectActor(SupportUserActor user, CancellationToken cancellationToken) {
        user.markDisconnected();
    }
}
