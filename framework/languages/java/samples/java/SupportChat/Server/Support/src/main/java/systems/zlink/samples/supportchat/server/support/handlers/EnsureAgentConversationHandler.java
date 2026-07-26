package systems.zlink.samples.supportchat.server.support.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ActorRefSnapshot;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.server.support.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.SupportChannel)
public final class EnsureAgentConversationHandler
    implements ZLinkRequestHandler<Messages.EnsureAgentConversationReq,
        Messages.EnsureAgentConversationRes> {
    private final ZLinkActorClient actorClient;
    private final ZLinkActorManager actors;
    private final SupportActorDirectory directory;

    public EnsureAgentConversationHandler(
        ZLinkActorClient actorClient,
        ZLinkActorManager actors,
        SupportActorDirectory directory) {
        this.actorClient = actorClient;
        this.actors = actors;
        this.directory = directory;
    }

    @Override
    public CompletionStage<Messages.EnsureAgentConversationRes> handle(
        Messages.EnsureAgentConversationReq request,
        ZLinkRequestContext context) {
        String conversationActorId = request.rosterActorId() + "@" + request.conversationId();
        return actors.find(conversationActorId).thenCompose(existing -> {
            if (existing.isPresent()) {
                ActorRef actorRef = existing.orElseThrow();
                return refresh(actorRef).thenApply(joined -> response(actorRef, joined));
            }
            Messages.EnsureSupportUserActorReq create = new Messages.EnsureSupportUserActorReq(
                conversationActorId,
                request.displayName(),
                SampleNames.Roles.Agent,
                request.rosterActorId());
            return actors.getOrCreate(conversationActorId, SampleNames.SupportActorType, create)
                .thenCompose(actorRef -> directory.require(conversationActorId).context()
                    .joinSpot(
                        RoutingId.from(request.conversationId()),
                        new Messages.JoinConversationReq(
                            request.rosterActorId(), SampleNames.Roles.Agent, request.displayName()))
                    .timeout(SampleTimings.RequestTimeout)
                    .submit(Messages.JoinConversationRes.class)
                    .thenCompose(result -> {
                        if (!(result instanceof ZLinkActorJoinResult.Accepted<?> accepted)) {
                            throw new IllegalStateException("Agent conversation join was rejected");
                        }
                        return refresh(accepted.actor())
                            .thenApply(joined -> response(actorRef, joined));
                    }));
        });
    }

    private CompletionStage<Messages.JoinConversationRes> refresh(ActorRef actorRef) {
        return actorClient.requestToActor(actorRef.actorId(), new Messages.JoinConversationReq())
            .timeout(SampleTimings.RequestTimeout)
            .submit(Messages.JoinConversationRes.class);
    }

    private static Messages.EnsureAgentConversationRes response(
        ActorRef actorRef,
        Messages.JoinConversationRes joined) {
        return new Messages.EnsureAgentConversationRes(
            ActorRefSnapshot.from(actorRef), joined.state());
    }
}
