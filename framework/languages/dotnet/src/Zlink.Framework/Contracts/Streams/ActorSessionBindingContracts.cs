namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkActorSessionBindingStore
{
    ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken);

    ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken);

    ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
        string actorId,
        CancellationToken cancellationToken);
}
public readonly record struct ZLinkActorSessionRoute(
    RoutingId SessionRouterId,
    string BindingToken);

public readonly record struct ZLinkActorSessionBinding(
    string ActorId,
    RoutingId SessionRouterId,
    string BindingToken);

public readonly record struct ZLinkActorSessionUnbind(
    string ActorId,
    string BindingToken);
