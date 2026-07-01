using System.Runtime.CompilerServices;
using SupportChat.Client.Configuration;
using SupportChat.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SupportChat.Client;

internal sealed class SupportChatClientScenario
{
    // ConversationId rides in stream metadata, so conversation packets carry it there
    // (not in the body) and the Session server routes each to the right room (§9.2).
    private const string Cid = SampleNames.ConversationIdMetadataKey;

    // End-to-end client story for one agent : many customers (§16, §17):
    // 1. One agent registers availability.
    // 2. Two customers open conversations; the same agent is assigned to both and joins each.
    // 3. Each room keeps its own MessageSeq; typing is one-way.
    // 4. The agent reconnects and re-joins both rooms.
    // 5. One room auto-closes on idle; the other is closed explicitly; then messages are rejected.
    // 6. With no available agent, a new customer stays WaitingForAgent.
    public async ValueTask RunAsync(
        IZlinkStreamConnector agent,
        IZlinkStreamConnector customer1,
        IZlinkStreamConnector customer2,
        IZlinkStreamConnector reconnectingAgent,
        IZlinkStreamConnector waitingCustomer,
        CancellationToken cancellationToken = default)
    {
        await agent.Connect.Async(cancellationToken);
        var agentAuth = await agent.Request(new AuthenticateReq("agent-1")).Async<AuthenticateRes>(cancellationToken);
        Ensure(agentAuth.ActorId == "agent-1");
        Ensure(agentAuth.Role == SupportChatRoles.Agent);
        Ensure((await agent.Request(new SetAgentAvailableReq(true))
            .Async<SetAgentAvailableRes>(cancellationToken)).IsAvailable);

        // Customer 1 opens a conversation; the agent's roster is notified, then the
        // agent joins the conversation which turns it Active.
        await customer1.Connect.Async(cancellationToken);
        Ensure((await customer1.Request(new AuthenticateReq("customer-1"))
            .Async<AuthenticateRes>(cancellationToken)).ActorId == "customer-1");
        var assigned1ForAgent = agent.WaitFor<ConversationAssignedNotify>().Async(cancellationToken);
        var opened1 = await customer1.Request(new OpenConversationReq("checkout payment failed"))
            .Async<OpenConversationRes>(cancellationToken);
        var cid1 = opened1.ConversationId;
        Ensure(opened1.State.Status == ConversationStatuses.WaitingForAgent);
        Ensure((await assigned1ForAgent).Payload.ConversationId == cid1);

        var joined1ForCustomer = customer1.WaitFor<ParticipantJoinedNotify>().Async(cancellationToken);
        var agentJoin1 = await JoinAsync(agent, cid1, cancellationToken);
        Ensure(agentJoin1.State.Status == ConversationStatuses.Active);
        Ensure(agentJoin1.State.AgentActorId == "agent-1");
        Ensure(agentJoin1.State.Subject == "checkout payment failed");
        var joined1 = await joined1ForCustomer;
        Ensure(joined1.Payload.ConversationId == cid1);
        Ensure(joined1.Payload.ActorId == "agent-1");
        Ensure(joined1.Payload.State.Status == ConversationStatuses.Active);

        // Agent greets, customer replies; sequence is assigned per conversation.
        var greeting1ForCustomer = customer1.WaitFor<ChatMessageNotify>().Async(cancellationToken);
        var greet1 = await SendChatAsync(agent, cid1, "How can I help?", cancellationToken);
        Ensure(greet1.Message.MessageSeq == 1UL);
        var greeting1 = await greeting1ForCustomer;
        Ensure(greeting1.Payload.ConversationId == cid1);
        Ensure(greeting1.Payload.Message.MessageSeq == 1UL);

        var reply1ForAgent = agent.WaitFor<ChatMessageNotify>().Async(cancellationToken);
        var reply1 = await SendChatAsync(customer1, cid1, "Payment keeps failing.", cancellationToken);
        Ensure(reply1.Message.MessageSeq == 2UL);
        var reply1Push = await reply1ForAgent;
        Ensure(reply1Push.Payload.ConversationId == cid1);
        Ensure(reply1Push.Payload.Message.MessageSeq == 2UL);

        // Customer 2 opens a second conversation handled by the SAME agent.
        await customer2.Connect.Async(cancellationToken);
        Ensure((await customer2.Request(new AuthenticateReq("customer-2"))
            .Async<AuthenticateRes>(cancellationToken)).ActorId == "customer-2");
        var assigned2ForAgent = agent.WaitFor<ConversationAssignedNotify>().Async(cancellationToken);
        var opened2 = await customer2.Request(new OpenConversationReq("cannot log in"))
            .Async<OpenConversationRes>(cancellationToken);
        var cid2 = opened2.ConversationId;
        Ensure(cid2 != cid1);
        Ensure(opened2.State.Status == ConversationStatuses.WaitingForAgent);
        Ensure((await assigned2ForAgent).Payload.ConversationId == cid2);

        var joined2ForCustomer = customer2.WaitFor<ParticipantJoinedNotify>().Async(cancellationToken);
        var agentJoin2 = await JoinAsync(agent, cid2, cancellationToken);
        Ensure(agentJoin2.State.Status == ConversationStatuses.Active);
        Ensure(agentJoin2.State.Subject == "cannot log in");
        Ensure((await joined2ForCustomer).Payload.ConversationId == cid2);

        // The rooms are independent: cid2 starts its own MessageSeq at 1.
        var greeting2ForCustomer = customer2.WaitFor<ChatMessageNotify>().Async(cancellationToken);
        var greet2 = await SendChatAsync(agent, cid2, "Let me check your account.", cancellationToken);
        Ensure(greet2.Message.MessageSeq == 1UL);
        var greeting2 = await greeting2ForCustomer;
        Ensure(greeting2.Payload.ConversationId == cid2);
        Ensure(greeting2.Payload.Message.MessageSeq == 1UL);

        // Typing is a one-way send; only the other participant is notified.
        var typingForCustomer1 = customer1.WaitFor<TypingChangedNotify>().Async(cancellationToken);
        agent.Send(new SetTypingReq(true)).Metadata(Cid, cid1).Submit(cancellationToken);
        var typing1 = await typingForCustomer1;
        Ensure(typing1.Payload.ConversationId == cid1);
        Ensure(typing1.Payload.ActorId == "agent-1");
        Ensure(typing1.Payload.IsTyping);

        // Reconnect the agent on a fresh session. Close the old connection first (as a
        // real reconnect does), re-declare availability so the roster's push route
        // follows the new session, then re-join both rooms.
        await agent.Close.Async(cancellationToken);
        await reconnectingAgent.Connect.Async(cancellationToken);
        Ensure((await reconnectingAgent.Request(new AuthenticateReq("agent-1"))
            .Async<AuthenticateRes>(cancellationToken)).ActorId == "agent-1");
        Ensure((await reconnectingAgent.Request(new SetAgentAvailableReq(true))
            .Async<SetAgentAvailableRes>(cancellationToken)).IsAvailable);
        Ensure((await JoinAsync(reconnectingAgent, cid1, cancellationToken)).State.Subject == "checkout payment failed");
        Ensure((await JoinAsync(reconnectingAgent, cid2, cancellationToken)).State.Subject == "cannot log in");

        // Arm cid1 idle + auto-close waiters (both sides) before cid1 can close.
        var idleTimeout = SampleNames.IdleTimeout + SampleNames.CloseGraceTimeout + SampleNames.RequestTimeout;
        var idle1ForCustomer = customer1.WaitFor<ConversationIdleNotify>().Timeout(idleTimeout).Async(cancellationToken);
        var idle1ForAgent = reconnectingAgent.WaitFor<ConversationIdleNotify>()
            .Where(m => m.Payload.ConversationId == cid1).Timeout(idleTimeout).Async(cancellationToken);
        var closed1ForCustomer = customer1.WaitFor<ConversationClosedNotify>().Timeout(idleTimeout).Async(cancellationToken);
        var closed1ForAgent = reconnectingAgent.WaitFor<ConversationClosedNotify>()
            .Where(m => m.Payload.ConversationId == cid1).Timeout(idleTimeout).Async(cancellationToken);

        // Explicitly close cid2 from the customer; only the agent is notified.
        var closed2ForAgent = reconnectingAgent.WaitFor<ConversationClosedNotify>()
            .Where(m => m.Payload.ConversationId == cid2).Timeout(idleTimeout).Async(cancellationToken);
        var closed2 = await customer2.Request(new CloseConversationReq("resolved")).Metadata(Cid, cid2)
            .Async<CloseConversationRes>(cancellationToken);
        Ensure(closed2.State.Status == ConversationStatuses.Closed);
        Ensure((await closed2ForAgent).Payload.ConversationId == cid2);

        // Closing an already-closed conversation returns an error response.
        await ExpectFailureAsync(
            customer2.Request(new CloseConversationReq("again")).Metadata(Cid, cid2)
                .Async<CloseConversationRes>(cancellationToken).AsTask(),
            "Closed conversation must reject a duplicate close.");

        // cid1 idles first (both sides notified WaitingForClose), then auto-closes.
        Ensure((await idle1ForCustomer).Payload.State.Status == ConversationStatuses.WaitingForClose);
        var idle1Agent = await idle1ForAgent;
        Ensure(idle1Agent.Payload.ConversationId == cid1);
        Ensure(idle1Agent.Payload.State.Status == ConversationStatuses.WaitingForClose);
        Ensure((await closed1ForCustomer).Payload.State.Status == ConversationStatuses.Closed);
        Ensure((await closed1ForAgent).Payload.ConversationId == cid1);

        // A closed conversation rejects further messages.
        await ExpectFailureAsync(
            SendChatAsync(customer1, cid1, "are you there?", cancellationToken).AsTask(),
            "Closed conversation must reject follow-up messages.");

        // With the agent unavailable and no capacity elsewhere, a new customer waits.
        Ensure(!(await reconnectingAgent.Request(new SetAgentAvailableReq(false))
            .Async<SetAgentAvailableRes>(cancellationToken)).IsAvailable);
        await waitingCustomer.Connect.Async(cancellationToken);
        Ensure((await waitingCustomer.Request(new AuthenticateReq("customer-3"))
            .Async<AuthenticateRes>(cancellationToken)).ActorId == "customer-3");

        // A customer actor cannot register agent availability.
        await ExpectFailureAsync(
            waitingCustomer.Request(new SetAgentAvailableReq(true))
                .Async<SetAgentAvailableRes>(cancellationToken).AsTask(),
            "Customer must not set agent availability.");

        var noAgentOpen = await waitingCustomer.Request(new OpenConversationReq("agent unavailable"))
            .Async<OpenConversationRes>(cancellationToken);
        Ensure(noAgentOpen.State.Status == ConversationStatuses.WaitingForAgent);
        Ensure(noAgentOpen.State.Subject == "agent unavailable");
        await ExpectNoPushAsync<ConversationClosedNotify>(
            waitingCustomer,
            TimeSpan.FromMilliseconds(500),
            cancellationToken);
    }

    private static ValueTask<JoinConversationRes> JoinAsync(
        IZlinkStreamConnector connector,
        string conversationId,
        CancellationToken cancellationToken)
    {
        return connector.Request(new JoinConversationReq()).Metadata(Cid, conversationId)
            .Async<JoinConversationRes>(cancellationToken);
    }

    private static ValueTask<SendChatMessageRes> SendChatAsync(
        IZlinkStreamConnector connector,
        string conversationId,
        string text,
        CancellationToken cancellationToken)
    {
        return connector.Request(new SendChatMessageReq(text)).Metadata(Cid, conversationId)
            .Async<SendChatMessageRes>(cancellationToken);
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))]
        string? expression = null)
    {
        if (!condition) throw new InvalidOperationException($"Ensure failed: {expression}");
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
            var message = await client.WaitFor<TPayload>().Timeout(timeout).Async(cancellationToken);
            throw new InvalidOperationException(
                $"Unexpected '{typeof(TPayload).Name}' push was received: {message.Name}.");
        }
        catch (TimeoutException)
        {
        }
    }
}
