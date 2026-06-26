package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.Map;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkTimer;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.notifications.ConversationNotificationPublisher;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.handlers.ConversationIdleTimerHandler;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.handlers.ConversationSpotCreatedHandler;
import systems.zlink.samples.supportchat.server.support.domain.conversation.Conversation;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationChange;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationCreateRequest;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationPolicy;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.Roles;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class ConversationSpot implements ZLinkSpot<SupportUserActor> {
    private final ZLinkSpotContext context;
    private final ConversationNotificationPublisher notifications;
    private final ConversationSpotCreatedHandler createdHandler;
    private final ObjectMapper json;
    private final Map<String, SupportUserActor> actors = new LinkedHashMap<>();
    private Conversation conversation;
    private ZLinkTimer idleTimer;

    public ConversationSpot(
        ZLinkSpotContext context,
        ConversationNotificationPublisher notifications,
        ConversationSpotCreatedHandler createdHandler,
        ObjectMapper json) {
        this.context = context;
        this.notifications = notifications;
        this.createdHandler = createdHandler;
        this.json = json;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        return createdHandler.handle(this, request);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        SupportUserActor user,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        Messages.JoinConversationReq join = request.decode(Messages.JoinConversationReq.class);
        Conversation current = requireConversation();
        if (!join.conversationId().equals(current.conversationId())) {
            return ZLinkSpotActorJoinResponse.reject();
        }
        if (Roles.Agent.equals(user.role())) {
            Messages.ConversationState agentState = joinAgent(user);
            return ZLinkSpotActorJoinResponse.accept(new Messages.JoinConversationRes(agentState));
        }
        user.joinConversation(join.conversationId());
        actors.put(user.actorId(), user);
        if (user.actorId().equals(current.customerActorId())) {
            notifications.publishJoinedAgentToCustomer(user, current.snapshot());
        }
        return ZLinkSpotActorJoinResponse.accept(new Messages.JoinConversationRes(current.snapshot()));
    }

    @Override
    public void onJoinedActor(SupportUserActor user, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(SupportUserActor user, CancellationToken cancellationToken) {
        actors.remove(user.actorId());
    }

    @Override
    public void onDisconnectActor(SupportUserActor user, CancellationToken cancellationToken) {
        user.markDisconnected();
    }

    @Override
    public void onInitialize() {
        idleTimer = await(context.addTimer(
            "conversation-idle",
            SampleTimings.IdleCheckPeriod,
            ConversationIdleTimerHandler.class,
            new ZLinkTimerOptions()));
    }

    @Override
    public void onClosing() {
        if (idleTimer != null) {
            await(idleTimer.cancelAsync());
        }
    }

    public void applyCreate(ConversationCreateRequest request) {
        this.conversation = new Conversation(
            context.spotRid().toString(),
            request.subject(),
            request.customerActorId(),
            request.customerDisplayName(),
            request.createdAtUnixMs(),
            new ConversationPolicy(
                SampleTimings.IdleTimeout.toMillis(),
                SampleTimings.CloseGraceTimeout.toMillis(),
                SampleTimings.MaxMessageLength));
    }

    public Messages.ConversationState joinAgent(SupportUserActor agent) {
        Conversation current = requireConversation();
        ConversationChange change = current.joinAgent(
            agent.actorId(),
            agent.displayName(),
            System.currentTimeMillis());
        agent.joinConversation(current.conversationId());
        actors.put(agent.actorId(), agent);
        notifications.publish(change.events(), actors);
        return change.state();
    }

    public Messages.SendChatMessageRes sendMessage(
        SupportUserActor actor,
        Messages.SendChatMessageReq request) {
        ensureConversationId(request.conversationId());
        ConversationChange change = requireConversation().sendMessage(
            actor.actorId(),
            request.text(),
            System.currentTimeMillis());
        notifications.publish(change.events(), actors);
        Messages.ChatMessage message = change.events().stream()
            .map(systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.ConversationEvent::message)
            .filter(item -> item != null)
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("Message event was not created."));
        return new Messages.SendChatMessageRes(message, change.state());
    }

    public Messages.SetTypingRes setTyping(
        SupportUserActor actor,
        Messages.SetTypingReq request) {
        ensureConversationId(request.conversationId());
        ConversationChange change = requireConversation().setTyping(actor.actorId(), request.isTyping());
        notifications.publish(change.events(), actors);
        return new Messages.SetTypingRes(change.state());
    }

    public Messages.CloseConversationRes close(
        SupportUserActor actor,
        Messages.CloseConversationReq request) {
        ensureConversationId(request.conversationId());
        ConversationChange change = requireConversation().close(actor.actorId(), request.reason());
        notifications.publish(change.events(), actors);
        return new Messages.CloseConversationRes(change.state());
    }

    public void checkIdle() {
        if (conversation == null) {
            return;
        }
        ConversationChange change = conversation.markIdle(System.currentTimeMillis());
        notifications.publish(change.events(), actors);
    }

    private void ensureConversationId(String conversationId) {
        Conversation current = requireConversation();
        if (!conversationId.equals(current.conversationId())) {
            throw new IllegalStateException("Request targets another conversation. conversation=" + conversationId);
        }
    }

    private Conversation requireConversation() {
        if (conversation == null) {
            throw new IllegalStateException("Conversation has not been created.");
        }
        return conversation;
    }

}
