namespace Zlink.Framework;

internal sealed class ZLinkActorRuntime(ZLinkFrameworkRuntime runtime) : IZLinkActorRuntime
{
    public ValueTask AttachAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        ArgumentNullException.ThrowIfNull(stream);
        return runtime.AttachActorAsync(actor, stream, cancellationToken);
    }

    public ValueTask DisconnectAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        ArgumentNullException.ThrowIfNull(stream);
        return runtime.DisconnectActorAsync(actor, stream, cancellationToken);
    }

    public ValueTask SubmitAsync(
        IZLinkActor actor,
        global::Zlink.Message header,
        global::Zlink.Message body,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        ArgumentNullException.ThrowIfNull(header);
        ArgumentNullException.ThrowIfNull(body);
        return runtime.SubmitActorAsync(actor, header, body, cancellationToken);
    }
}
