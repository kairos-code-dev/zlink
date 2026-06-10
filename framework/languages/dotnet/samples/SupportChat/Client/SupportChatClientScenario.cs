using System.Runtime.CompilerServices;
using SupportChat.Shared.Configuration;
using SupportChat.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;

namespace SupportChat.Client;

internal sealed class SupportChatClientScenario
{
    public async ValueTask RunAsync(
        IZlinkStreamConnector customer,
        IZlinkStreamConnector agent,
        IZlinkStreamConnector reconnectingCustomer,
        IZlinkStreamConnector waitingCustomer,
        IZlinkStreamConnector closingCustomer,
        IZlinkStreamConnector closingAgent,
        CancellationToken cancellationToken = default)
    {
        await agent.ConnectAsync(cancellationToken);
        var agentAuth = await agent.Request(new AuthenticateReq("agent-1")).SubmitAsync<AuthenticateRes>(cancellationToken);
        Ensure(agentAuth.ActorId == "agent-1");
        Ensure(agentAuth.Role == SupportChatRoles.Agent);

        var available = await agent.Request(new SetAgentAvailableReq(true)).SubmitAsync<SetAgentAvailableRes>(cancellationToken);
        Ensure(available.IsAvailable);

        await customer.ConnectAsync(cancellationToken);
        var customerAuth = await customer.Request(new AuthenticateReq("customer-1")).SubmitAsync<AuthenticateRes>(cancellationToken);
        Ensure(customerAuth.ActorId == "customer-1");
        Ensure(customerAuth.Role == SupportChatRoles.Customer);

        var assignedForAgent = agent.WaitFor<ConversationAssignedNotify>().SubmitAsync(cancellationToken);
        var joinedForCustomer = customer.WaitFor<ParticipantJoinedNotify>().SubmitAsync(cancellationToken);
        var opened = await customer.Request(new OpenConversationReq("checkout payment failed")).SubmitAsync<OpenConversationRes>(cancellationToken);
        Ensure(opened.State.CustomerActorId == customerAuth.ActorId);
        Ensure(opened.State.Status is ConversationStatuses.WaitingForAgent or ConversationStatuses.Active);

        var joined = await joinedForCustomer;
        Ensure(joined.Payload.ActorId == agentAuth.ActorId);
        Ensure(joined.Payload.ConversationId == opened.ConversationId);
        Ensure(joined.Payload.Role == SupportChatRoles.Agent);
        Ensure(joined.Payload.State.Status == ConversationStatuses.Active);

        var assigned = await assignedForAgent;
        Ensure(assigned.Payload.ConversationId == opened.ConversationId);
        Ensure(assigned.Payload.State.AgentActorId == agentAuth.ActorId);

        var agentTypingForCustomer = customer.WaitFor<TypingChangedNotify>().SubmitAsync(cancellationToken);
        var typing = await agent.Request(new SetTypingReq(opened.ConversationId, true)).SubmitAsync<SetTypingRes>(cancellationToken);
        Ensure(typing.State.Status == ConversationStatuses.Active);
        var agentTypingForCustomerPush = await agentTypingForCustomer;
        Ensure(agentTypingForCustomerPush.Payload.ActorId == agentAuth.ActorId);
        Ensure(agentTypingForCustomerPush.Payload.IsTyping);

        var agentMessageForCustomer = customer.WaitFor<ChatMessageNotify>().SubmitAsync(cancellationToken);
        var sentByAgent = await agent.Request(new SendChatMessageReq(opened.ConversationId, "I can help with a new payment link.")).SubmitAsync<SendChatMessageRes>(cancellationToken);
        Ensure(sentByAgent.Message.MessageSeq == 1UL);
        Ensure(sentByAgent.State.LastMessageSeq == 1UL);
        var agentMessageForCustomerPush = await agentMessageForCustomer;
        Ensure(agentMessageForCustomerPush.Payload.Message.SenderActorId == agentAuth.ActorId);
        Ensure(agentMessageForCustomerPush.Payload.Message.Text == "I can help with a new payment link.");

        await reconnectingCustomer.ConnectAsync(cancellationToken);
        var reconnected = await reconnectingCustomer.Request(new AuthenticateReq("customer-1")).SubmitAsync<AuthenticateRes>(cancellationToken);
        Ensure(reconnected.ActorId == customerAuth.ActorId);

        var customerMessageForAgent = agent.WaitFor<ChatMessageNotify>().SubmitAsync(cancellationToken);
        var sentByCustomer = await reconnectingCustomer.Request(new SendChatMessageReq(opened.ConversationId, "I cannot complete payment.")).SubmitAsync<SendChatMessageRes>(cancellationToken);
        Ensure(sentByCustomer.Message.MessageSeq == 2UL);
        var customerMessageForAgentPush = await customerMessageForAgent;
        Ensure(customerMessageForAgentPush.Payload.Message.SenderActorId == customerAuth.ActorId);
        Ensure(customerMessageForAgentPush.Payload.Message.Text == "I cannot complete payment.");

        var followUpForCustomer = reconnectingCustomer.WaitFor<ChatMessageNotify>().SubmitAsync(cancellationToken);
        var followUpByAgent = await agent.Request(new SendChatMessageReq(opened.ConversationId, "Please try this replacement link.")).SubmitAsync<SendChatMessageRes>(cancellationToken);
        Ensure(followUpByAgent.Message.MessageSeq == 3UL);
        var followUpForCustomerPush = await followUpForCustomer;
        Ensure(followUpForCustomerPush.Payload.Message.SenderActorId == agentAuth.ActorId);
        Ensure(followUpForCustomerPush.Payload.State.LastMessageSeq == 3UL);

        var idleTimeout = SampleTimings.IdleTimeout + SampleTimings.CloseGraceTimeout + SampleTimings.RequestTimeout;
        var idleForCustomer = reconnectingCustomer.WaitFor<ConversationIdleNotify>().Timeout(idleTimeout).SubmitAsync(cancellationToken);
        var idleForAgent = agent.WaitFor<ConversationIdleNotify>().Timeout(idleTimeout).SubmitAsync(cancellationToken);
        var idleForCustomerPush = await idleForCustomer;
        Ensure(idleForCustomerPush.Payload.ConversationId == opened.ConversationId);
        Ensure(idleForCustomerPush.Payload.State.Status == ConversationStatuses.WaitingForClose);
        var idleForAgentPush = await idleForAgent;
        Ensure(idleForAgentPush.Payload.ConversationId == opened.ConversationId);
        Ensure(idleForAgentPush.Payload.State.Status == ConversationStatuses.WaitingForClose);

        var closedForAgent = agent.WaitFor<ConversationClosedNotify>().SubmitAsync(cancellationToken);
        var closed = await reconnectingCustomer.Request(new CloseConversationReq(opened.ConversationId, "resolved")).SubmitAsync<CloseConversationRes>(cancellationToken);
        Ensure(closed.State.Status == ConversationStatuses.Closed);
        var closedForAgentPush = await closedForAgent;
        Ensure(closedForAgentPush.Payload.ConversationId == opened.ConversationId);
        Ensure(closedForAgentPush.Payload.State.Status == ConversationStatuses.Closed);

        await ExpectFailureAsync(
            reconnectingCustomer.Request(new SendChatMessageReq(opened.ConversationId, "after close")).SubmitAsync<SendChatMessageRes>(cancellationToken).AsTask(),
            "Closed conversation must reject follow-up messages.");

        await waitingCustomer.ConnectAsync(cancellationToken);
        var waitingCustomerAuth = await waitingCustomer.Request(new AuthenticateReq("customer-2")).SubmitAsync<AuthenticateRes>(cancellationToken);
        Ensure(waitingCustomerAuth.ActorId == "customer-2");
        var noAgentConversation = await waitingCustomer.Request(new OpenConversationReq("agent unavailable")).SubmitAsync<OpenConversationRes>(cancellationToken);
        Ensure(noAgentConversation.State.Status == ConversationStatuses.WaitingForAgent);
        Ensure(noAgentConversation.State.Subject == "agent unavailable");
        await ExpectNoPushAsync<ConversationClosedNotify>(
            waitingCustomer,
            TimeSpan.FromMilliseconds(500),
            cancellationToken);

        await closingAgent.ConnectAsync(cancellationToken);
        var closingAgentAuth = await closingAgent.Request(new AuthenticateReq("agent-2")).SubmitAsync<AuthenticateRes>(cancellationToken);
        Ensure(closingAgentAuth.ActorId == "agent-2");
        var secondAgentAvailable = await closingAgent.Request(new SetAgentAvailableReq(true)).SubmitAsync<SetAgentAvailableRes>(cancellationToken);
        Ensure(secondAgentAvailable.IsAvailable);

        await closingCustomer.ConnectAsync(cancellationToken);
        var closingCustomerAuth = await closingCustomer.Request(new AuthenticateReq("customer-3")).SubmitAsync<AuthenticateRes>(cancellationToken);
        Ensure(closingCustomerAuth.ActorId == "customer-3");
        var explicitAssignedForAgent = closingAgent.WaitFor<ConversationAssignedNotify>().SubmitAsync(cancellationToken);
        var explicitJoinedForCustomer = closingCustomer.WaitFor<ParticipantJoinedNotify>().SubmitAsync(cancellationToken);
        var explicitOpened = await closingCustomer.Request(new OpenConversationReq("explicit close")).SubmitAsync<OpenConversationRes>(cancellationToken);
        var explicitJoinedForCustomerPush = await explicitJoinedForCustomer;
        Ensure(explicitJoinedForCustomerPush.Payload.ActorId == closingAgentAuth.ActorId);
        Ensure(explicitJoinedForCustomerPush.Payload.State.Status == ConversationStatuses.Active);
        Ensure((await explicitAssignedForAgent).Payload.State.AgentActorId == closingAgentAuth.ActorId);

        var explicitClosedForAgent = closingAgent.WaitFor<ConversationClosedNotify>().SubmitAsync(cancellationToken);
        var explicitClosed = await closingCustomer.Request(new CloseConversationReq(explicitOpened.ConversationId, "done")).SubmitAsync<CloseConversationRes>(cancellationToken);
        Ensure(explicitClosed.State.Status == ConversationStatuses.Closed);
        var explicitClosedForAgentPush = await explicitClosedForAgent;
        Ensure(explicitClosedForAgentPush.Payload.ConversationId == explicitOpened.ConversationId);
        Ensure(explicitClosedForAgentPush.Payload.State.Status == ConversationStatuses.Closed);
        await ExpectFailureAsync(
            closingCustomer.Request(new CloseConversationReq(explicitOpened.ConversationId, "again")).SubmitAsync<CloseConversationRes>(cancellationToken).AsTask(),
            "Closed conversation must reject duplicate close.");
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))] string? expression = null)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"Ensure failed: {expression}");
        }
    }

    private static async Task ExpectFailureAsync(Task request, string message)
    {
        try
        {
            await request;
        }
        catch (Exception error) when (error is not OperationCanceledException)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }

    private static async Task ExpectNoPushAsync<TPayload>(
        IZlinkStreamConnector client,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        try
        {
            var message = await client.WaitFor<TPayload>().Timeout(timeout).SubmitAsync(cancellationToken);
            throw new InvalidOperationException($"Unexpected '{typeof(TPayload).Name}' push was received: {message.Name}.");
        }
        catch (TimeoutException)
        {
        }
    }
}
