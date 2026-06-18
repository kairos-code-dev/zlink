package systems.zlink.samples.supportchat.client;

import java.time.Duration;
import java.util.concurrent.CompletionException;
import java.util.concurrent.TimeoutException;
import systems.zlink.samples.supportchat.client.configuration.SampleNames;
import systems.zlink.samples.supportchat.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class SupportChatClientScenario {
    public void run(
        ZLinkStreamConnector customer,
        ZLinkStreamConnector agent,
        ZLinkStreamConnector reconnectingCustomer,
        ZLinkStreamConnector waitingCustomer,
        ZLinkStreamConnector closingCustomer,
        ZLinkStreamConnector closingAgent) throws Exception {
        agent.connect().await();
        Messages.AuthenticateRes agentAuth =
            agent.request(new Messages.AuthenticateReq("agent-1")).await(Messages.AuthenticateRes.class);
        ensure(agentAuth.actorId().equals("agent-1"));
        ensure(agentAuth.role().equals(SampleNames.AgentRole));

        Messages.SetAgentAvailableRes available =
            agent.request(new Messages.SetAgentAvailableReq(true)).await(Messages.SetAgentAvailableRes.class);
        ensure(available.isAvailable());

        customer.connect().await();
        Messages.AuthenticateRes customerAuth =
            customer.request(new Messages.AuthenticateReq("customer-1")).await(Messages.AuthenticateRes.class);
        ensure(customerAuth.actorId().equals("customer-1"));
        ensure(customerAuth.role().equals(SampleNames.CustomerRole));

        var assignedForAgent =
            agent.waitFor(SampleNames.ConversationAssignedPacket).submit(Messages.ConversationAssignedNotify.class);
        var joinedForCustomer =
            customer.waitFor(SampleNames.ParticipantJoinedPacket).submit(Messages.ParticipantJoinedNotify.class);
        Messages.OpenConversationRes opened =
            customer.request(new Messages.OpenConversationReq("checkout payment failed"))
                .await(Messages.OpenConversationRes.class);
        ensure(opened.state().customerActorId().equals(customerAuth.actorId()));
        ensure(opened.state().status().equals(SampleNames.WaitingForAgent)
            || opened.state().status().equals(SampleNames.Active));

        Messages.ParticipantJoinedNotify joined = customer.await(joinedForCustomer).payload();
        ensure(joined.actorId().equals(agentAuth.actorId()));
        ensure(joined.conversationId().equals(opened.conversationId()));
        ensure(joined.role().equals(SampleNames.AgentRole));
        ensure(joined.state().status().equals(SampleNames.Active));

        Messages.ConversationAssignedNotify assigned = agent.await(assignedForAgent).payload();
        ensure(assigned.conversationId().equals(opened.conversationId()));
        ensure(assigned.state().agentActorId().equals(agentAuth.actorId()));

        var agentTypingForCustomer =
            customer.waitFor(SampleNames.TypingChangedPacket).submit(Messages.TypingChangedNotify.class);
        Messages.SetTypingRes typing =
            agent.request(new Messages.SetTypingReq(opened.conversationId(), true)).await(Messages.SetTypingRes.class);
        ensure(typing.state().status().equals(SampleNames.Active));
        Messages.TypingChangedNotify typingPush = customer.await(agentTypingForCustomer).payload();
        ensure(typingPush.actorId().equals(agentAuth.actorId()));
        ensure(typingPush.isTyping());

        var agentMessageForCustomer =
            customer.waitFor(SampleNames.ChatMessagePacket).submit(Messages.ChatMessageNotify.class);
        Messages.SendChatMessageRes sentByAgent = agent
            .request(new Messages.SendChatMessageReq(opened.conversationId(), "I can help with a new payment link."))
            .await(Messages.SendChatMessageRes.class);
        ensure(sentByAgent.message().messageSeq() == 1L);
        ensure(sentByAgent.state().lastMessageSeq() == 1L);
        Messages.ChatMessageNotify agentMessagePush = customer.await(agentMessageForCustomer).payload();
        ensure(agentMessagePush.message().senderActorId().equals(agentAuth.actorId()));
        ensure(agentMessagePush.message().text().equals("I can help with a new payment link."));

        reconnectingCustomer.connect().await();
        Messages.AuthenticateRes reconnected = reconnectingCustomer
            .request(new Messages.AuthenticateReq("customer-1")).await(Messages.AuthenticateRes.class);
        ensure(reconnected.actorId().equals(customerAuth.actorId()));

        var customerMessageForAgent =
            agent.waitFor(SampleNames.ChatMessagePacket).submit(Messages.ChatMessageNotify.class);
        Messages.SendChatMessageRes sentByCustomer = reconnectingCustomer
            .request(new Messages.SendChatMessageReq(opened.conversationId(), "I cannot complete payment."))
            .await(Messages.SendChatMessageRes.class);
        ensure(sentByCustomer.message().messageSeq() == 2L);
        Messages.ChatMessageNotify customerMessagePush = agent.await(customerMessageForAgent).payload();
        ensure(customerMessagePush.message().senderActorId().equals(customerAuth.actorId()));
        ensure(customerMessagePush.message().text().equals("I cannot complete payment."));

        var followUpForCustomer =
            reconnectingCustomer.waitFor(SampleNames.ChatMessagePacket).submit(Messages.ChatMessageNotify.class);
        Messages.SendChatMessageRes followUpByAgent = agent
            .request(new Messages.SendChatMessageReq(opened.conversationId(), "Please try this replacement link."))
            .await(Messages.SendChatMessageRes.class);
        ensure(followUpByAgent.message().messageSeq() == 3L);
        Messages.ChatMessageNotify followUpPush = reconnectingCustomer.await(followUpForCustomer).payload();
        ensure(followUpPush.message().senderActorId().equals(agentAuth.actorId()));
        ensure(followUpPush.state().lastMessageSeq() == 3L);

        Duration idleTimeout = SampleNames.IdleTimeout
            .plus(SampleNames.CloseGraceTimeout)
            .plus(SampleNames.RequestTimeout);
        var idleForCustomer = reconnectingCustomer.waitFor(SampleNames.ConversationIdlePacket)
            .timeout(idleTimeout).submit(Messages.ConversationIdleNotify.class);
        var idleForAgent = agent.waitFor(SampleNames.ConversationIdlePacket)
            .timeout(idleTimeout).submit(Messages.ConversationIdleNotify.class);
        Messages.ConversationIdleNotify idleCustomerPush = reconnectingCustomer.await(idleForCustomer).payload();
        ensure(idleCustomerPush.conversationId().equals(opened.conversationId()));
        ensure(idleCustomerPush.state().status().equals(SampleNames.WaitingForClose));
        Messages.ConversationIdleNotify idleAgentPush = agent.await(idleForAgent).payload();
        ensure(idleAgentPush.conversationId().equals(opened.conversationId()));
        ensure(idleAgentPush.state().status().equals(SampleNames.WaitingForClose));

        var closedForAgent =
            agent.waitFor(SampleNames.ConversationClosedPacket).submit(Messages.ConversationClosedNotify.class);
        Messages.CloseConversationRes closed = reconnectingCustomer
            .request(new Messages.CloseConversationReq(opened.conversationId(), "resolved"))
            .await(Messages.CloseConversationRes.class);
        ensure(closed.state().status().equals(SampleNames.Closed));
        Messages.ConversationClosedNotify closedPush = agent.await(closedForAgent).payload();
        ensure(closedPush.conversationId().equals(opened.conversationId()));
        ensure(closedPush.state().status().equals(SampleNames.Closed));

        expectFailure(
            () -> reconnectingCustomer
                .request(new Messages.SendChatMessageReq(opened.conversationId(), "after close"))
                .await(Messages.SendChatMessageRes.class),
            "Closed conversation must reject follow-up messages.");

        waitingCustomer.connect().await();
        Messages.AuthenticateRes waitingCustomerAuth = waitingCustomer
            .request(new Messages.AuthenticateReq("customer-2")).await(Messages.AuthenticateRes.class);
        ensure(waitingCustomerAuth.actorId().equals("customer-2"));
        Messages.OpenConversationRes noAgentConversation = waitingCustomer
            .request(new Messages.OpenConversationReq("agent unavailable")).await(Messages.OpenConversationRes.class);
        ensure(noAgentConversation.state().status().equals(SampleNames.WaitingForAgent));
        ensure(noAgentConversation.state().subject().equals("agent unavailable"));
        expectNoPush(waitingCustomer, SampleNames.ConversationClosedPacket, Duration.ofMillis(500));
        expectFailure(
            () -> waitingCustomer
                .request(new Messages.SetAgentAvailableReq(true))
                .await(Messages.SetAgentAvailableRes.class),
            "Customer actor SetAgentAvailableReq must be rejected.");

        closingAgent.connect().await();
        Messages.AuthenticateRes closingAgentAuth = closingAgent
            .request(new Messages.AuthenticateReq("agent-2")).await(Messages.AuthenticateRes.class);
        ensure(closingAgentAuth.actorId().equals("agent-2"));
        Messages.SetAgentAvailableRes secondAgentAvailable = closingAgent
            .request(new Messages.SetAgentAvailableReq(true)).await(Messages.SetAgentAvailableRes.class);
        ensure(secondAgentAvailable.isAvailable());

        closingCustomer.connect().await();
        Messages.AuthenticateRes closingCustomerAuth = closingCustomer
            .request(new Messages.AuthenticateReq("customer-3")).await(Messages.AuthenticateRes.class);
        ensure(closingCustomerAuth.actorId().equals("customer-3"));
        var explicitAssignedForAgent = closingAgent
            .waitFor(SampleNames.ConversationAssignedPacket).submit(Messages.ConversationAssignedNotify.class);
        var explicitJoinedForCustomer = closingCustomer
            .waitFor(SampleNames.ParticipantJoinedPacket).submit(Messages.ParticipantJoinedNotify.class);
        Messages.OpenConversationRes explicitOpened = closingCustomer
            .request(new Messages.OpenConversationReq("explicit close")).await(Messages.OpenConversationRes.class);
        Messages.ParticipantJoinedNotify explicitJoinedPush =
            closingCustomer.await(explicitJoinedForCustomer).payload();
        ensure(explicitJoinedPush.actorId().equals(closingAgentAuth.actorId()));
        ensure(explicitJoinedPush.state().status().equals(SampleNames.Active));
        ensure(closingAgent.await(explicitAssignedForAgent).payload()
            .state().agentActorId().equals(closingAgentAuth.actorId()));

        var explicitClosedForAgent = closingAgent
            .waitFor(SampleNames.ConversationClosedPacket).submit(Messages.ConversationClosedNotify.class);
        Messages.CloseConversationRes explicitClosed = closingCustomer
            .request(new Messages.CloseConversationReq(explicitOpened.conversationId(), "done"))
            .await(Messages.CloseConversationRes.class);
        ensure(explicitClosed.state().status().equals(SampleNames.Closed));
        Messages.ConversationClosedNotify explicitClosedPush =
            closingAgent.await(explicitClosedForAgent).payload();
        ensure(explicitClosedPush.conversationId().equals(explicitOpened.conversationId()));
        ensure(explicitClosedPush.state().status().equals(SampleNames.Closed));
        expectFailure(
            () -> closingCustomer
                .request(new Messages.CloseConversationReq(explicitOpened.conversationId(), "again"))
                .await(Messages.CloseConversationRes.class),
            "Closed conversation must reject duplicate close.");
    }

    private interface Action {
        void run() throws Exception;
    }

    private static void expectFailure(Action action, String message) throws Exception {
        try {
            action.run();
        } catch (Exception error) {
            return;
        }
        throw new IllegalStateException(message);
    }

    private void expectNoPush(
        ZLinkStreamConnector client,
        String packetName,
        Duration timeout) throws Exception {
        try {
            var push = client.waitFor(packetName).timeout(timeout).submit();
            client.await(push);
        } catch (TimeoutException expected) {
            return;
        } catch (CompletionException error) {
            if (error.getCause() instanceof TimeoutException) {
                return;
            }
            throw error;
        }
        throw new IllegalStateException("Unexpected '" + packetName + "' push was received.");
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }
}
