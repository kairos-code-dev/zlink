using SpotActorTransfer.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;

namespace SpotActorTransfer.SessionGateway;

internal sealed class BindActorSessionHandler(
    IZLinkActorManager actors,
    GatewayEvidenceStore evidence) : IZLinkSessionPacketHandler<IZLinkSessionContext, BindActorSessionReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        BindActorSessionReq request,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var actorRef = await actors.FindAsync(request.ActorId, cancellationToken);
        ActorRef resolved = actorRef ?? (request.NodeRid is not null && request.Generation is not null
            ? new ActorRef(RoutingId.From(request.NodeRid), request.ActorId, checked((ulong)request.Generation.Value))
            : throw new InvalidOperationException($"Actor '{request.ActorId}' was not found."));
        _ = await context.Actors.BindOrGetAsync(resolved, cancellationToken).ConfigureAwait(false);
        evidence.Add(request.Scenario, request.ActorId, "session_bound", context.SessionId);
        await context.Client.Reply(new BindActorSessionRes(
                request.Scenario, resolved.ActorId, resolved.NodeRid.ToString(), checked((long)resolved.Generation)))
            .SubmitAsync(cancellationToken);
    }
}

internal sealed class TransferSession(
    IZLinkSessionContext context,
    GatewayEvidenceStore evidence) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public void Configure() => Context.Handlers.AddHandler<BindActorSessionHandler>();

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add("session", Context.SessionId, "connected", Context.RoutingId?.ToString() ?? "");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add("session", Context.SessionId, "disconnected", Context.RoutingId?.ToString() ?? "");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add("session", Context.SessionId, "error", error.ToString());
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (await Context.Handlers.TryHandleAsync(dispatch, payload, cancellationToken).ConfigureAwait(false)) return;
        var actor = Context.Actors.Bound.SingleOrDefault()
                    ?? throw new InvalidOperationException("No actor is bound.");
        await actor.RelayAsync(payload, cancellationToken).ConfigureAwait(false);
    }
}
