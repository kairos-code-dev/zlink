using Bingo.Shared.Configuration;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;

namespace Bingo.Server.Session;

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
                cancellationToken)
            .ConfigureAwait(false);
        state.ActorRoute = route;
        return state.Actor;
    }
}
