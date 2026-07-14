using System.Runtime.CompilerServices;
using SupportChat.Client.Configuration;
using SupportChat.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SupportChat.Client;

internal sealed class SupportChatClientScenario
{
    private const string Cid = SampleNames.ConversationIdMetadataKey;

    // End-to-end client story for one agent : many customers (§16, §17):
    // 1. One agent registers availability.
    // 2. Two customers open conversations; the same agent is assigned to both and joins each.
    // 3. Each room keeps its own MessageSeq; typing is one-way.
    // 4. The customer and agent reconnect and re-join their rooms.
    // 5. One room auto-closes on idle; the other is closed explicitly; then messages are rejected.
    // 6. With no available agent, a new customer stays WaitingForAgent.
    public async ValueTask RunAsync(
        IZlinkStreamConnector agent,
        IZlinkStreamConnector customer1,
        IZlinkStreamConnector customer2,
        IZlinkStreamConnector reconnectingAgent,
        IZlinkStreamConnector reconnectingCustomer,
        IZlinkStreamConnector waitingCustomer,
        CancellationToken cancellationToken = default)
    {
        await agent.Connect.Async(cancellationToken);
        await ExpectFailureAsync(
            agent.Request(new OpenConversationReq("unauthenticated"))
                .Async<OpenConversationRes>(cancellationToken).AsTask(),
            "Unauthenticated client must not open a conversation.");
        await ExpectFailureAsync(
            agent.Request(new SendChatMessageReq("unauthenticated"))
                .Metadata(Cid, "missing-conversation")
                .Async<SendChatMessageRes>(cancellationToken).AsTask(),
            "Unauthenticated client must not send chat messages.");

        var agentAuth = await agent.Request(new AuthenticateReq("agent-1")).Async<AuthenticateRes>(cancellationToken);
        Ensure(agentAuth.ActorId == "agent-1");
        Ensure(agentAuth.Role == SupportChatRoles.Agent);
        await ExpectFailureAsync(
            agent.Request(new OpenConversationReq("agent cannot open"))
                .Async<OpenConversationRes>(cancellationToken).AsTask(),
            "Agent must not open a customer conversation.");
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
        var agentRoom1 = Conversation(agent, cid1);
        var customerRoom1 = Conversation(customer1, cid1);
        var agentJoin1 = await agentRoom1.JoinAsync(cancellationToken);
        Ensure(agentJoin1.State.Status == ConversationStatuses.Active);
        Ensure(agentJoin1.State.AgentActorId == "agent-1");
        Ensure(agentJoin1.State.Subject == "checkout payment failed");
        var joined1 = await joined1ForCustomer;
        Ensure(joined1.Payload.ConversationId == cid1);
        Ensure(joined1.Payload.ActorId == "agent-1");
        Ensure(joined1.Payload.State.Status == ConversationStatuses.Active);

        // Agent greets, customer replies; sequence is assigned per conversation.
        var greeting1ForCustomer = customer1.WaitFor<ChatMessageNotify>().Async(cancellationToken);
        var greet1 = await agentRoom1.SendChatAsync("How can I help?", cancellationToken);
        Ensure(greet1.Message.MessageSeq == 1UL);
        var greeting1 = await greeting1ForCustomer;
        Ensure(greeting1.Payload.ConversationId == cid1);
        Ensure(greeting1.Payload.Message.MessageSeq == 1UL);

        var reply1ForAgent = agent.WaitFor<ChatMessageNotify>().Async(cancellationToken);
        var reply1 = await customerRoom1.SendChatAsync("Payment keeps failing.", cancellationToken);
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
        var agentRoom2 = Conversation(agent, cid2);
        var customerRoom2 = Conversation(customer2, cid2);
        var agentJoin2 = await agentRoom2.JoinAsync(cancellationToken);
        Ensure(agentJoin2.State.Status == ConversationStatuses.Active);
        Ensure(agentJoin2.State.Subject == "cannot log in");
        Ensure((await joined2ForCustomer).Payload.ConversationId == cid2);

        // The rooms are independent: cid2 starts its own MessageSeq at 1.
        var greeting2ForCustomer = customer2.WaitFor<ChatMessageNotify>().Async(cancellationToken);
        var greet2 = await agentRoom2.SendChatAsync("Let me check your account.", cancellationToken);
        Ensure(greet2.Message.MessageSeq == 1UL);
        var greeting2 = await greeting2ForCustomer;
        Ensure(greeting2.Payload.ConversationId == cid2);
        Ensure(greeting2.Payload.Message.MessageSeq == 1UL);

        // Typing is a one-way send; only the other participant is notified.
        var typingForCustomer1 = customer1.WaitFor<TypingChangedNotify>().Async(cancellationToken);
        agentRoom1.SendTyping(true, cancellationToken);
        var typing1 = await typingForCustomer1;
        Ensure(typing1.Payload.ConversationId == cid1);
        Ensure(typing1.Payload.ActorId == "agent-1");
        Ensure(typing1.Payload.IsTyping);

        // Reconnect customer 1 with the same token and re-read the current room state.
        await customer1.Close.Async(cancellationToken);
        await reconnectingCustomer.Connect.Async(cancellationToken);
        Ensure((await reconnectingCustomer.Request(new AuthenticateReq("customer-1"))
            .Async<AuthenticateRes>(cancellationToken)).ActorId == "customer-1");
        customerRoom1 = Conversation(reconnectingCustomer, cid1);
        var customerRejoin1 = await customerRoom1.JoinAsync(cancellationToken);
        Ensure(customerRejoin1.State.Subject == "checkout payment failed");
        Ensure(customerRejoin1.State.Status == ConversationStatuses.Active);
        Ensure(customerRejoin1.State.LastMessageSeq == 2UL);

        // Reconnect the agent on a fresh session. Close the old connection first (as a
        // real reconnect does), re-declare availability so the roster's push route
        // follows the new session, then re-join both rooms.
        await agent.Close.Async(cancellationToken);
        await reconnectingAgent.Connect.Async(cancellationToken);
        Ensure((await reconnectingAgent.Request(new AuthenticateReq("agent-1"))
            .Async<AuthenticateRes>(cancellationToken)).ActorId == "agent-1");
        Ensure((await reconnectingAgent.Request(new SetAgentAvailableReq(true))
            .Async<SetAgentAvailableRes>(cancellationToken)).IsAvailable);
        var reconnectedRoom1 = Conversation(reconnectingAgent, cid1);
        var reconnectedRoom2 = Conversation(reconnectingAgent, cid2);
        Ensure((await reconnectedRoom1.JoinAsync(cancellationToken)).State.Subject == "checkout payment failed");
        Ensure((await reconnectedRoom2.JoinAsync(cancellationToken)).State.Subject == "cannot log in");

        // Arm cid1 idle + auto-close waiters (both sides) before cid1 can close.
        var idleTimeout = SampleNames.IdleTimeout + SampleNames.CloseGraceTimeout + SampleNames.RequestTimeout;
        var idle1ForCustomer = reconnectingCustomer.WaitFor<ConversationIdleNotify>().Timeout(idleTimeout).Async(cancellationToken);
        var idle1ForAgent = reconnectingAgent.WaitFor<ConversationIdleNotify>()
            .Where(m => m.Payload.ConversationId == cid1).Timeout(idleTimeout).Async(cancellationToken);
        var closed1ForCustomer = reconnectingCustomer.WaitFor<ConversationClosedNotify>().Timeout(idleTimeout).Async(cancellationToken);
        var closed1ForAgent = reconnectingAgent.WaitFor<ConversationClosedNotify>()
            .Where(m => m.Payload.ConversationId == cid1).Timeout(idleTimeout).Async(cancellationToken);

        // Explicitly close cid2 from the customer; only the agent is notified.
        var closed2ForAgent = reconnectingAgent.WaitFor<ConversationClosedNotify>()
            .Where(m => m.Payload.ConversationId == cid2).Timeout(idleTimeout).Async(cancellationToken);
        var closed2 = await customerRoom2.CloseAsync("resolved", cancellationToken);
        Ensure(closed2.State.Status == ConversationStatuses.Closed);
        var closed2Agent = await closed2ForAgent;
        Ensure(closed2Agent.Payload.ConversationId == cid2);
        Ensure(closed2Agent.Payload.State.Status == ConversationStatuses.Closed);

        // Closing an already-closed conversation returns an error response.
        await ExpectFailureAsync(
            customerRoom2.CloseAsync("again", cancellationToken).AsTask(),
            "Closed conversation must reject a duplicate close.");

        // cid1 idles first (both sides notified WaitingForClose), then auto-closes.
        Ensure((await idle1ForCustomer).Payload.State.Status == ConversationStatuses.WaitingForClose);
        var idle1Agent = await idle1ForAgent;
        Ensure(idle1Agent.Payload.ConversationId == cid1);
        Ensure(idle1Agent.Payload.State.Status == ConversationStatuses.WaitingForClose);
        Ensure((await closed1ForCustomer).Payload.State.Status == ConversationStatuses.Closed);
        var closed1Agent = await closed1ForAgent;
        Ensure(closed1Agent.Payload.ConversationId == cid1);
        Ensure(closed1Agent.Payload.State.Status == ConversationStatuses.Closed);

        // A closed conversation rejects further messages.
        await ExpectFailureAsync(
            customerRoom1.SendChatAsync("are you there?", cancellationToken).AsTask(),
            "Closed conversation must reject follow-up messages.");
        var closedTypingForAgent = reconnectingAgent.WaitFor<TypingChangedNotify>()
            .Where(m => m.Payload.ConversationId == cid1).Timeout(TimeSpan.FromMilliseconds(500))
            .Async(cancellationToken);
        customerRoom1.SendTyping(true, cancellationToken);
        await ExpectTimeoutAsync(
            closedTypingForAgent.AsTask(),
            "Closed conversation must ignore typing sends without notifying participants.");
        Console.WriteLine("supportchat-closed-typing-ignore=verified");

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
        // customer-3 now belongs to its own room. Supplying cid2 must not silently send this
        // message to customer-3's current room.
        await ExpectFailureAsync(
            waitingCustomer.Request(new SendChatMessageReq("not a participant"))
                .Metadata(Cid, cid2)
                .Async<SendChatMessageRes>(cancellationToken).AsTask(),
            "Non-participant must not send chat messages.");
        await ExpectNoPushAsync<ConversationClosedNotify>(
            waitingCustomer,
            TimeSpan.FromMilliseconds(500),
            cancellationToken);
    }

    private static ConversationClient Conversation(IZlinkStreamConnector connector, string conversationId) =>
        new(connector, conversationId);

    private readonly struct ConversationClient(IZlinkStreamConnector connector, string conversationId)
    {
        // ConversationId rides in stream metadata, not in the body. Keeping that detail
        // here lets the scenario read as conversation work instead of route wiring.
        public ValueTask<JoinConversationRes> JoinAsync(CancellationToken cancellationToken)
        {
            return connector.Request(new JoinConversationReq()).Metadata(Cid, conversationId)
                .Async<JoinConversationRes>(cancellationToken);
        }

        public ValueTask<SendChatMessageRes> SendChatAsync(string text, CancellationToken cancellationToken)
        {
            return connector.Request(new SendChatMessageReq(text)).Metadata(Cid, conversationId)
                .Async<SendChatMessageRes>(cancellationToken);
        }

        public void SendTyping(bool isTyping, CancellationToken cancellationToken)
        {
            connector.Send(new SetTypingReq(isTyping)).Metadata(Cid, conversationId).Submit(cancellationToken);
        }

        public ValueTask<CloseConversationRes> CloseAsync(string? reason, CancellationToken cancellationToken)
        {
            return connector.Request(new CloseConversationReq(reason)).Metadata(Cid, conversationId)
                .Async<CloseConversationRes>(cancellationToken);
        }
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
        catch (ZlinkStreamException error) when (error.Error.Code == ZlinkStreamErrorCode.RemoteError)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }

    private static async Task ExpectTimeoutAsync(Task request, string message)
    {
        try
        {
            await request;
        }
        catch (TimeoutException)
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
