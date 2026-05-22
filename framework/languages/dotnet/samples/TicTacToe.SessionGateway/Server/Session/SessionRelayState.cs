using Systems.Zlink;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class SessionRelayState
{
    public IZLinkActorRef? Actor { get; private set; }

    public string? ActorId { get; private set; }

    public ZLinkActorRoute? ActorRoute { get; private set; }

    public void AttachAuthenticatedActor(
        string actorId,
        ActorRouteSnapshot route)
    {
        var actorRoute = new ZLinkActorRoute(
            route.RouterChannelId,
            RoutingId.FromBytes(route.TargetNodeRid),
            route.ActorGeneration);

        if (!string.Equals(ActorId, actorId, StringComparison.Ordinal)
            || ActorRoute != actorRoute)
        {
            Actor = null;
        }

        ActorId = actorId;
        ActorRoute = actorRoute;
    }

    public string RequireActorId(string action)
    {
        return ActorId
            ?? throw new InvalidOperationException($"Client must authenticate before {action}.");
    }

    public async ValueTask<IZLinkActorRef> RequireActorAsync(
        IZLinkSessionContext context,
        string action,
        CancellationToken cancellationToken)
    {
        var actorId = RequireActorId(action);
        var actorRoute = ActorRoute
            ?? throw new InvalidOperationException($"Actor route must be attached before {action}.");

        if (Actor is not null)
        {
            return Actor;
        }

        Actor = await context.BindActorHandleAsync(
                actorId,
                SampleNames.PlayerActorType,
                actorRoute,
                cancellationToken)
            .ConfigureAwait(false);
        return Actor;
    }

    public void Clear()
    {
        Actor = null;
        ActorId = null;
        ActorRoute = null;
    }
}
