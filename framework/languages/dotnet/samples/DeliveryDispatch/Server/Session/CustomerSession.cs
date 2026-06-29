using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.Session;

internal sealed class CustomerSession(
    IZLinkSessionContext context,
    IZLinkSessionPacketDispatcher<IZLinkSessionContext> handlers,
    CustomerSessionDirectory sessions) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        sessions.Add(Context);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        sessions.Remove(Context);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken)
    {
        _ = error;
        _ = cancellationToken;
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

        var actor = Context.Actors.Bound.Single();
        await actor.RelayAsync(payload, cancellationToken);
    }
}
