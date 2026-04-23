namespace Zlink.Framework;

public interface IZLinkActorRuntime
{
    ValueTask AttachAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default);

    ValueTask DisconnectAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default);

    ValueTask SubmitAsync(
        IZLinkActor actor,
        global::Zlink.Message header,
        global::Zlink.Message body,
        CancellationToken cancellationToken = default);
}
