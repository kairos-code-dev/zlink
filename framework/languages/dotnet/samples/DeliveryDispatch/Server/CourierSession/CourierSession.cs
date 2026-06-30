using Zlink.Framework.Contracts.Streams;
using DeliveryDispatch.Shared.Contracts;

namespace DeliveryDispatch.Server.CourierSession;

internal sealed class CourierSession(
    IZLinkSessionContext context,
    IZLinkSessionPacketDispatcher<IZLinkSessionContext> handlers) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        foreach (var actor in Context.Actors.Bound)
        {
            await actor.NotifyDisconnectedAsync(cancellationToken);
        }
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (await handlers.TryHandleAsync(Context, dispatch, payload, cancellationToken))
        {
            return;
        }

        var decision = payload.Decode<CourierDecision>();
        var actor = Context.Actors.Find(decision.CourierId)
            ?? throw new InvalidOperationException($"Courier actor is not bound: {decision.CourierId}");
        await actor.RelayAsync(payload, cancellationToken);
    }
}
