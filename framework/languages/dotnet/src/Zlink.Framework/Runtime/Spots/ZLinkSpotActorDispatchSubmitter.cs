
namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorDispatchSubmitter(
    ZLinkSpotSerialExecutor serial,
    ZLinkSpotActivationDispatcher dispatcher)
{
    public async ValueTask SubmitAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var ownedBody = body.Move();

        try
        {
            await serial.ExecuteAsync(
                async static (_, state, ct) =>
                {
                    using var currentBody = state.Body;
                    await state.Dispatcher.DispatchActorPacketAsync(
                            state.Actor,
                            state.RuntimeState,
                            state.Header,
                            currentBody,
                            ct)
                        .ConfigureAwait(false);
                },
                new ActorDispatchState(dispatcher, actor, runtimeState, header, ownedBody),
                cancellationToken).ConfigureAwait(false);
        }
        catch
        {
            ownedBody.Dispose();
            throw;
        }
    }

    public async ValueTask<byte[]> SubmitForReplyAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var ownedBody = body.Move();

        try
        {
            var state = new ActorReplyDispatchState(dispatcher, actor, runtimeState, header, ownedBody);
            await serial.ExecuteAsync(
                async static (_, state, ct) =>
                {
                    using var currentBody = state.Body;
                    state.Reply = await state.Dispatcher.DispatchActorPacketForReplyAsync(
                            state.Actor,
                            state.RuntimeState,
                            state.Header,
                            currentBody,
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
            ownedBody.Dispose();
            throw;
        }
    }

    private sealed class ActorDispatchState(
        ZLinkSpotActivationDispatcher dispatcher,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body)
    {
        public ZLinkSpotActivationDispatcher Dispatcher { get; } = dispatcher;

        public IZLinkActor Actor { get; } = actor;

        public ZLinkActorRuntimeState RuntimeState { get; } = runtimeState;

        public ZlinkStreamHeader Header { get; } = header;

        public Message Body { get; } = body;
    }

    private sealed class ActorReplyDispatchState(
        ZLinkSpotActivationDispatcher dispatcher,
        IZLinkActor actor,
        ZLinkActorRuntimeState runtimeState,
        ZlinkStreamHeader header,
        Message body)
    {
        public ZLinkSpotActivationDispatcher Dispatcher { get; } = dispatcher;

        public IZLinkActor Actor { get; } = actor;

        public ZLinkActorRuntimeState RuntimeState { get; } = runtimeState;

        public ZlinkStreamHeader Header { get; } = header;

        public Message Body { get; } = body;

        public byte[]? Reply { get; set; }
    }
}
