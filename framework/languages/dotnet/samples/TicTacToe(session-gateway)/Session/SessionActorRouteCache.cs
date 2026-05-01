using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Session;

internal sealed class SessionActorRouteCache
{
    public async ValueTask<IZLinkActorRef> EnsureRouteAsync(
        IZLinkSessionContext context,
        SessionRelayState state,
        ZLinkPlayRoute route,
        CancellationToken cancellationToken)
    {
        var actorId = state.RequireActorId("sending game packets");
        if (state.Actor is not null && state.ActorRoute == route)
        {
            return state.Actor;
        }

        state.Actor = await context.CreateRemoteActorAsync(
                route.PlayRouterId,
                actorId,
                SampleNames.PlayerActorType,
                cancellationToken)
            .ConfigureAwait(false);
        state.ActorRoute = route;
        return state.Actor;
    }
}
