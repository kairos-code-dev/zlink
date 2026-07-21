using ObservabilityOps.Server.Session.Support;
using ObservabilityOps.Server.Support;
using ObservabilityOps.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;

namespace ObservabilityOps.Server.Session.Handlers;

internal sealed class AuthenticateHandler(
    IZLinkSpotClient routes,
    IZLinkSpotHandleResolver spots,
    ObservabilityOps.Server.Session.Support.SessionOptions options)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, AuthenticateReq>
{
    public async ValueTask HandleAsync(IZLinkSessionContext context, ZLinkSessionDispatchContext dispatch,
        AuthenticateReq request, CancellationToken cancellationToken)
    {
        _ = dispatch;
        var entry = await spots.ResolveSpotHandleAsync(
                        ObservabilityNames.PlayMesh,
                        RoutingId.From(options.PreferredPlayRid),
                        cancellationToken)
                    ?? throw new InvalidOperationException("Preferred Play entry spot was not found.");
        var ensured = await routes.RequestToSpot(entry, new EnsurePlayerReq(request.ActorId))
            .Async<EnsurePlayerRes>(cancellationToken);
        await context.Actors.BindOrGetAsync(new ActorRef(RoutingId.From(ensured.NodeRid),
            ensured.ActorId, ensured.Generation), cancellationToken);
        await context.Client.Reply(new AuthenticateRes(ensured.ActorId, ensured.NodeRid, ensured.Generation))
            .SubmitAsync(cancellationToken);
    }
}

internal sealed class SessionBoundedOperationHandler(BoundedOperationGate gate)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, SessionBoundedOperationReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        SessionBoundedOperationReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        await gate.EnterAsync(cancellationToken);
        await context.Client.Reply(new SessionBoundedOperationRes(request.Marker))
            .SubmitAsync(cancellationToken);
    }
}
