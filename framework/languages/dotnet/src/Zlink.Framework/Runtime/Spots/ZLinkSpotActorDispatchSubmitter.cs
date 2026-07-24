namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorDispatchSubmitter(
    ZLinkSpotSerialExecutor serial,
    ZLinkSpotActorPacketDispatcher dispatcher)
{
    public async ValueTask Async(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var ownedPayload = payload.Copy();

        try
        {
            await serial.ExecuteActorAsync(
                runtimeState.ActorId,
                async static (_, state, ct) =>
                {
                    using var currentPayload = state.Payload;
                    await state.Dispatcher.DispatchAsync(
                            state.Actor,
                            state.RuntimeState,
                            state.Header,
                            currentPayload,
                            ct)
                        .ConfigureAwait(false);
                },
                new ActorDispatchState(dispatcher, actor, runtimeState, header, ownedPayload),
                cancellationToken).ConfigureAwait(false);
        }
        catch
        {
            ownedPayload.Dispose();
            throw;
        }
    }

    public async ValueTask<ZLinkActorReply> SubmitForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        var ownedPayload = payload.Copy();

        try
        {
            var state = new ActorReplyDispatchState(dispatcher, actor, runtimeState, header, ownedPayload);
            await serial.ExecuteActorAsync(
                runtimeState.ActorId,
                async static (_, state, ct) =>
                {
                    using var currentPayload = state.Payload;
                    state.Reply = await state.Dispatcher.DispatchForReplyAsync(
                            state.Actor,
                            state.RuntimeState,
                            state.Header,
                            currentPayload,
                            ct)
                        .ConfigureAwait(false);
                },
                state,
                cancellationToken).ConfigureAwait(false);

            return state.Reply
                   ?? throw new InvalidOperationException(
                       $"SPOT actor packet reply for '{header.Name}' was null.");
        }
        catch
        {
            ownedPayload.Dispose();
            throw;
        }
    }

    private sealed class ActorDispatchState(
        ZLinkSpotActorPacketDispatcher dispatcher,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload)
    {
        public ZLinkSpotActorPacketDispatcher Dispatcher { get; } = dispatcher;

        public IZLinkActor Actor { get; } = actor;

        public ZLinkActorRuntimeState RuntimeState { get; } = runtimeState;

        public ZlinkStreamHeader Header { get; } = header;

        public Message Payload { get; } = payload;
    }

    private sealed class ActorReplyDispatchState(
        ZLinkSpotActorPacketDispatcher dispatcher,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message payload)
    {
        public ZLinkSpotActorPacketDispatcher Dispatcher { get; } = dispatcher;

        public IZLinkActor Actor { get; } = actor;

        public ZLinkActorRuntimeState RuntimeState { get; } = runtimeState;

        public ZlinkStreamHeader Header { get; } = header;

        public Message Payload { get; } = payload;

        public ZLinkActorReply? Reply { get; set; }
    }
}
