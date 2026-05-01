using TicTacToe.SessionActorDispatch.Infrastructure;

namespace TicTacToe.SessionActorDispatch.Session;

internal interface IInitialActorRouteResolver
{
    ValueTask<ZLinkPlayRoute> ResolveAsync(
        string actorId,
        CancellationToken cancellationToken);
}

internal sealed class StaticInitialActorRouteResolver(ZLinkPlayRoute route) : IInitialActorRouteResolver
{
    public ValueTask<ZLinkPlayRoute> ResolveAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        _ = actorId;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(route);
    }
}
