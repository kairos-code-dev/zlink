namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorRelocationAdapter<TActor>
    where TActor : class, IZLinkActor
{
    ValueTask<byte[]> CaptureAsync(
        TActor actor,
        CancellationToken cancellationToken);

    ValueTask RestoreAsync(
        TActor actor,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken);
}

public abstract class ZLinkRelocationPolicy<TInstance>
    where TInstance : class
{
    private protected ZLinkRelocationPolicy()
    {
    }

    public static ZLinkRelocationPolicy<TInstance> Disabled { get; } =
        new DisabledPolicy();

    public static ZLinkRelocationPolicy<TInstance> Recreate { get; } =
        new RecreatePolicy();

    public static ZLinkRelocationPolicy<TInstance> Snapshot<TAdapter>()
        where TAdapter : class =>
        new SnapshotPolicy(typeof(TAdapter));

    internal abstract byte Kind { get; }

    internal virtual Type? AdapterType => null;

    private sealed class DisabledPolicy : ZLinkRelocationPolicy<TInstance>
    {
        internal override byte Kind => 0;
    }

    private sealed class RecreatePolicy : ZLinkRelocationPolicy<TInstance>
    {
        internal override byte Kind => 1;
    }

    private sealed class SnapshotPolicy(Type adapterType)
        : ZLinkRelocationPolicy<TInstance>
    {
        internal override byte Kind => 2;

        internal override Type AdapterType { get; } = adapterType;
    }
}
