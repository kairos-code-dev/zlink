namespace Systems.Zlink.Stream.Connector.Runtime;

internal readonly record struct ZlinkStreamLifecycleWorkerScope(
    object Owner,
    ZlinkStreamLifecycleWorkKind WorkKind)
{
    private static readonly AsyncLocal<Lease?> Ambient = new();

    public static (object Owner, ZlinkStreamLifecycleWorkKind WorkKind)? Current
    {
        get
        {
            var lease = Ambient.Value;
            return lease is { Active: true }
                ? (lease.Owner, lease.WorkKind)
                : null;
        }
    }

    public static ZlinkStreamLifecycleWorkKind? CurrentWorkKindFor(object owner)
    {
        var current = Current;
        return current is { } metadata && ReferenceEquals(metadata.Owner, owner)
            ? metadata.WorkKind
            : null;
    }

    public IDisposable Enter()
    {
        var previous = Ambient.Value;
        var current = new Lease(Owner, WorkKind);
        Ambient.Value = current;
        return new Scope(previous, current);
    }

    private sealed class Lease(object owner, ZlinkStreamLifecycleWorkKind workKind)
    {
        public object Owner { get; } = owner;

        public ZlinkStreamLifecycleWorkKind WorkKind { get; } = workKind;

        public bool Active { get; set; } = true;
    }

    private sealed class Scope(Lease? previous, Lease current) : IDisposable
    {
        private int _disposed;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
            current.Active = false;
            if (ReferenceEquals(Ambient.Value, current)) Ambient.Value = previous;
        }
    }
}

internal static class ZlinkStreamCallbackInvocationPermit
{
    private static readonly AsyncLocal<Lease?> Ambient = new();

    public static ZlinkStreamLifecycleWorkKind? CurrentWorkKindFor(object owner)
    {
        var lease = Ambient.Value;
        return lease is { Active: true } && ReferenceEquals(lease.Owner, owner)
            ? lease.WorkKind
            : null;
    }

    public static IDisposable? EnterCurrentWorker()
    {
        var worker = ZlinkStreamLifecycleWorkerScope.Current;
        if (worker is null) return null;

        var previous = Ambient.Value;
        var current = new Lease(worker.Value.Owner, worker.Value.WorkKind);
        Ambient.Value = current;
        return new Scope(previous, current);
    }

    private sealed class Lease(object owner, ZlinkStreamLifecycleWorkKind workKind)
    {
        public object Owner { get; } = owner;

        public ZlinkStreamLifecycleWorkKind WorkKind { get; } = workKind;

        public bool Active { get; set; } = true;
    }

    private sealed class Scope(Lease? previous, Lease current) : IDisposable
    {
        private int _disposed;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
            current.Active = false;
            if (ReferenceEquals(Ambient.Value, current)) Ambient.Value = previous;
        }
    }
}

internal enum ZlinkStreamLifecycleWorkKind
{
    ActiveConnect,
    Receive,
    Heartbeat,
    CloseCompletion
}
