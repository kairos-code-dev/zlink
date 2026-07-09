package systems.zlink.samples.supportchat.client;

import java.net.URI;
import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.server.configuration.SampleTopology;
import systems.zlink.samples.supportchat.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class Program {
    private Program() {
    }

    public static void main(String[] args) throws Exception {
        ZLinkStreamConnector customer = createClient();
        ZLinkStreamConnector agent = createClient();
        try {
            new SupportChatClientScenario().run(customer, agent);
            System.out.println(SampleNames.ClientMarker);
        } finally {
            customer.close();
            agent.close();
        }
    }

    private static ZLinkStreamConnector createClient() {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(SampleTopology.SessionStreamEndpoint),
            ZLinkStreamDispatchMode.AUTO,
            SampleTimings.RequestTimeout,
            SampleTimings.RequestTimeout,
            2,
            Duration.ofSeconds(5),
            64 * 1024,
            64 * 1024,
            Integer.MAX_VALUE,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false,
            null,
            null,
            null,
            null));
    }
}

final class SupportChatClientScenario {
    public void run(ZLinkStreamConnector customer, ZLinkStreamConnector agent) throws Exception {
        customer.connect().await();
        agent.connect().await();

        Messages.AuthenticateRes customerAuth = customer
            .request(new Messages.AuthenticateReq("customer-token"))
            .await(Messages.AuthenticateRes.class);
        ensure("customer".equals(customerAuth.role()));
        Messages.AuthenticateRes agentAuth = agent
            .request(new Messages.AuthenticateReq("agent-token"))
            .await(Messages.AuthenticateRes.class);
        ensure("agent".equals(agentAuth.role()));

        Messages.SetAgentAvailableRes available = agent
            .request(new Messages.SetAgentAvailableReq(true))
            .await(Messages.SetAgentAvailableRes.class);
        ensure(available.isAvailable());

        CompletionStage<ZLinkStreamMessage<Messages.ParticipantJoinedNotify>> customerJoined =
            waitJoined(customer, "customer-1");
        CompletionStage<ZLinkStreamMessage<Messages.ConversationAssignedNotify>> assigned =
            customer.waitFor(Messages.ConversationAssignedNotify.class)
                .submit(Messages.ConversationAssignedNotify.class);

        Messages.OpenConversationRes opened = customer
            .request(new Messages.OpenConversationReq("Order #42 needs help"))
            .await(Messages.OpenConversationRes.class);
        ensure(opened.conversationId().startsWith("conv-"));
        ensure(customer.await(customerJoined).payload().conversationId().equals(opened.conversationId()));
        ensure(customer.await(assigned).payload().conversationId().equals(opened.conversationId()));

        CompletionStage<ZLinkStreamMessage<Messages.ParticipantJoinedNotify>> agentJoined =
            waitJoined(agent, "agent-1");
        CompletionStage<ZLinkStreamMessage<Messages.ParticipantJoinedNotify>> customerSeesAgent =
            waitJoined(customer, "agent-1");
        Messages.JoinConversationRes joined = agent
            .request(new Messages.JoinConversationReq(opened.conversationId()))
            .await(Messages.JoinConversationRes.class);
        ensure("agent-1".equals(joined.state().agentActorId()));
        ensure(agent.await(agentJoined).payload().conversationId().equals(opened.conversationId()));
        ensure(customer.await(customerSeesAgent).payload().conversationId().equals(opened.conversationId()));

        CompletionStage<ZLinkStreamMessage<Messages.TypingChangedNotify>> typing =
            customer.waitFor(Messages.TypingChangedNotify.class)
                .where(Messages.TypingChangedNotify.class, message ->
                    message.payload().conversationId().equals(opened.conversationId()))
                .submit(Messages.TypingChangedNotify.class);
        agent.send(new Messages.SetTypingReq(opened.conversationId(), true)).submit();
        ensure(customer.await(typing).payload().isTyping());

        CompletionStage<ZLinkStreamMessage<Messages.ChatMessageNotify>> customerMessage =
            waitMessage(agent, opened.conversationId(), "customer-1");
        Messages.SendChatMessageRes customerSent = customer
            .request(new Messages.SendChatMessageReq(opened.conversationId(), "My package is late."))
            .await(Messages.SendChatMessageRes.class);
        ensure(customerSent.message().messageSeq() == 1);
        ensure(agent.await(customerMessage).payload().message().messageSeq() == 1);

        CompletionStage<ZLinkStreamMessage<Messages.ChatMessageNotify>> agentMessage =
            waitMessage(customer, opened.conversationId(), "agent-1");
        Messages.SendChatMessageRes agentSent = agent
            .request(new Messages.SendChatMessageReq(opened.conversationId(), "I will check it now."))
            .await(Messages.SendChatMessageRes.class);
        ensure(agentSent.message().messageSeq() == 2);
        ensure(customer.await(agentMessage).payload().message().messageSeq() == 2);

        CompletionStage<ZLinkStreamMessage<Messages.ConversationClosedNotify>> closed =
            customer.waitFor(Messages.ConversationClosedNotify.class)
                .where(Messages.ConversationClosedNotify.class, message ->
                    message.payload().conversationId().equals(opened.conversationId()))
                .submit(Messages.ConversationClosedNotify.class);
        Messages.CloseConversationRes closeResponse = customer
            .request(new Messages.CloseConversationReq(opened.conversationId(), "resolved"))
            .await(Messages.CloseConversationRes.class);
        ensure("Closed".equals(closeResponse.state().status()));
        ensure(customer.await(closed).payload().conversationId().equals(opened.conversationId()));
        System.out.println("supportchat-conversation=" + opened.conversationId());
    }

    private static CompletionStage<ZLinkStreamMessage<Messages.ParticipantJoinedNotify>> waitJoined(
        ZLinkStreamConnector connector,
        String actorId) {
        return connector.waitFor(Messages.ParticipantJoinedNotify.class)
            .where(Messages.ParticipantJoinedNotify.class, message -> message.payload().actorId().equals(actorId))
            .submit(Messages.ParticipantJoinedNotify.class);
    }

    private static CompletionStage<ZLinkStreamMessage<Messages.ChatMessageNotify>> waitMessage(
        ZLinkStreamConnector connector,
        String conversationId,
        String sender) {
        return connector.waitFor(Messages.ChatMessageNotify.class)
            .where(Messages.ChatMessageNotify.class, message ->
                message.payload().conversationId().equals(conversationId)
                    && message.payload().message().senderActorId().equals(sender))
            .submit(Messages.ChatMessageNotify.class);
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("SupportChat client self-check failed");
        }
    }
}
