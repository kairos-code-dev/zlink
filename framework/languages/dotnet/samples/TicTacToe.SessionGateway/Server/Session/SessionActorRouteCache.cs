using TicTacToe.SessionGateway.Shared.Configuration;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class SessionActorRouteCache
{
    public async ValueTask<IZLinkActorRef> EnsureRouteAsync(
        IZLinkSessionContext context,
        SessionRelayState state,
        ZLinkActorRoute route,
        CancellationToken cancellationToken)
    {
        var actorId = state.RequireActorId("sending game packets");
        if (state.Actor is not null && state.ActorRoute == route)
        {
            return state.Actor;
        }

        state.Actor = await context.BindActorHandleAsync(
                actorId,
                SampleNames.PlayerActorType,
                route,
                cancellationToken)
            .ConfigureAwait(false);
        state.ActorRoute = route;
        return state.Actor;
    }

    public ValueTask<IZLinkActorRef> EnsureRouteAsync(
        IZLinkSessionContext context,
        SessionRelayState state,
        CancellationToken cancellationToken)
    {
        if (state.Actor is not null)
        {
            return ValueTask.FromResult(state.Actor);
        }

        if (state.ActorRoute is { } route)
        {
            return EnsureRouteAsync(context, state, route, cancellationToken);
        }

        throw new InvalidOperationException("Actor route is not attached.");
    }
}
