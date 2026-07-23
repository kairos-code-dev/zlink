namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeBundle : IAsyncDisposable
{
    private readonly HashSet<string> _autoConnections = new(StringComparer.Ordinal);
    private readonly object _connectionGate = new();
    private readonly object _disposeGate = new();
    private readonly HashSet<string> _manualConnections = new(StringComparer.Ordinal);
    private int _disposed;
    private long _autoConnectAttemptCount;
    private long _autoDisconnectAttemptCount;
    private Task? _disposeTask;

    public ZLinkChannelRuntimeBundle(
        IZLinkBackendSocket socket,
        ZLinkAsyncSubmitter? submitter = null,
        RoutingId localRid = default,
        string? socketRole = null,
        ZLinkClientServerServerIdentity? clientServerServer = null)
    {
        Socket = socket;
        Submitter = submitter;
        LocalRid = localRid.Size > 0 ? localRid.ToString() : null;
        SocketRole = socketRole;
        ClientServerServer = clientServerServer;
    }

    public IZLinkBackendSocket Socket { get; }

    public ZLinkAsyncSubmitter? Submitter { get; }

    public string? LocalRid { get; }

    public string? SocketRole { get; }

    internal ZLinkClientServerServerIdentity? ClientServerServer { get; }

    public SemaphoreSlim ReceiveGate { get; } = new(1, 1);

    internal long AutoConnectAttemptCount =>
        Volatile.Read(ref _autoConnectAttemptCount);

    internal long AutoDisconnectAttemptCount =>
        Volatile.Read(ref _autoDisconnectAttemptCount);

    public ValueTask DisposeAsync()
    {
        Task task;
        TaskCompletionSource? start = null;
        lock (_disposeGate)
        {
            if (_disposeTask is null)
            {
                Volatile.Write(ref _disposed, 1);
                start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _disposeTask = DisposeCoreAsync(start.Task);
            }
            task = _disposeTask;
        }
        start?.TrySetResult();
        return new ValueTask(task);
    }

    private async Task DisposeCoreAsync(Task started)
    {
        await started.ConfigureAwait(false);
        var failures = new ZLinkFailureCollector();
        if (Submitter is not null)
            await failures.CaptureAsync(Submitter.DisposeAsync).ConfigureAwait(false);

        await failures.CaptureAsync(Socket.DisposeAsync).ConfigureAwait(false);
        failures.Capture(ReceiveGate.Dispose);
        failures.ThrowIfAny();
    }

    public void ConnectManual(IZLinkBackendConnectableSocket socket, string endpoint)
    {
        lock (_connectionGate) Acquire(_manualConnections, endpoint, () => socket.Connect(endpoint), true);
    }

    public void DisconnectManual(IZLinkBackendConnectableSocket socket, string endpoint)
    {
        lock (_connectionGate) Release(_manualConnections, endpoint, () => socket.Disconnect(endpoint), true);
    }

    public bool ConnectAuto(IZLinkBackendConnectableSocket socket, string endpoint)
    {
        lock (_connectionGate)
            return Acquire(
                _autoConnections,
                endpoint,
                () =>
                {
                    Interlocked.Increment(ref _autoConnectAttemptCount);
                    socket.Connect(endpoint);
                },
                false);
    }

    public bool DisconnectAuto(IZLinkBackendConnectableSocket socket, string endpoint)
    {
        lock (_connectionGate)
            return Release(
                _autoConnections,
                endpoint,
                () =>
                {
                    Interlocked.Increment(ref _autoDisconnectAttemptCount);
                    socket.Disconnect(endpoint);
                },
                false);
    }

    private bool Acquire(HashSet<string> source, string endpoint, Action connect, bool throwOnFailure)
    {
        var alreadyOwned = _manualConnections.Contains(endpoint) || _autoConnections.Contains(endpoint);
        if (!source.Add(endpoint) || alreadyOwned) return true;
        try
        {
            connect();
            return true;
        }
        catch when (!throwOnFailure)
        {
            source.Remove(endpoint);
            return false;
        }
        catch
        {
            source.Remove(endpoint);
            throw;
        }
    }

    private bool Release(HashSet<string> source, string endpoint, Action disconnect, bool throwOnFailure)
    {
        if (!source.Remove(endpoint)) return true;
        if (_manualConnections.Contains(endpoint) || _autoConnections.Contains(endpoint)) return true;
        try
        {
            disconnect();
            return true;
        }
        catch when (!throwOnFailure)
        {
            source.Add(endpoint);
            return false;
        }
        catch
        {
            source.Add(endpoint);
            throw;
        }
    }

}
