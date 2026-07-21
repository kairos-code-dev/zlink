namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkRuntimeState : IAsyncDisposable
{
    private readonly object _disposeGate = new();
    private Task? _disposeTask;

    public ZLinkFrameworkRuntimeState(
        IZLinkBackendContext context,
        ZLinkFrameworkRegistration registration,
        IServiceProvider services,
        ZLinkRuntimeErrorSink errorSink,
        object executionOwner)
    {
        Context = context;
        Registration = registration;
        ErrorSink = errorSink;
        TaskRunner = new ZLinkRuntimeTaskRunner(
            ErrorSink,
            StopTokenSource.Token,
            executionOwner,
            ownsSupervisor: true);
        MessageFlowObservers = new ZLinkMessageFlowObserverPump(
            registration.DispatchOptions,
            services,
            TaskRunner);
    }

    public IZLinkBackendContext Context { get; }

    public ZLinkFrameworkRegistration Registration { get; }

    public object SyncRoot { get; } = new();

    public CancellationTokenSource StopTokenSource { get; } = new();

    public ZLinkRuntimeTaskRunner TaskRunner { get; }

    public ZLinkRuntimeErrorSink ErrorSink { get; }

    public ZLinkMessageFlowObserverPump MessageFlowObservers { get; }

    public Dictionary<string, ZLinkChannelRuntimeBundle> SubscriberBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> PublisherBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotNodeRuntime> SpotNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkStreamNodeRuntime> StreamNodes { get; } = new(StringComparer.Ordinal);

    public List<Task> ListenerTasks { get; } = [];

    public bool TryGetSpotNodeByRoutingId(
        RoutingId nodeRid,
        out ZLinkSpotNodeRuntime nodeRuntime)
    {
        foreach (var candidate in SpotNodes.Values)
            if (candidate.Node.RoutingId == nodeRid)
            {
                nodeRuntime = candidate;
                return true;
            }

        nodeRuntime = null!;
        return false;
    }

    public void CancelActiveSpotOperations()
    {
        foreach (var node in SpotNodes.Values) node.CancelActiveOperations();
    }

    public void ForceStopStreamSessions()
    {
        ZLinkStreamNodeRuntime[] streams;
        lock (SyncRoot) streams = StreamNodes.Values.ToArray();
        foreach (var stream in streams) stream.ForceStopSessions();
    }

    public ValueTask DisposeAsync()
    {
        lock (_disposeGate)
        {
            if (_disposeTask is not null) return new ValueTask(_disposeTask);

            RuntimeResources resources;
            lock (SyncRoot)
            {
                resources = new RuntimeResources(
                    SpotNodes.Values.ToArray(),
                    StreamNodes.Values.ToArray(),
                    PublisherBundles.Values.ToArray(),
                    SubscriberBundles.Values.ToArray(),
                    ListenerTasks.ToArray());
            }

            return new ValueTask(_disposeTask = DisposeCoreAsync(resources));
        }
    }

    private async Task DisposeCoreAsync(RuntimeResources resources)
    {
        var failures = new List<Exception>();
        foreach (var node in resources.SpotNodes)
            await CaptureAsync(node.CloseLifecycleAsync).ConfigureAwait(false);

        Capture(StopTokenSource.Cancel);
        foreach (var node in resources.SpotNodes) Capture(node.RequestStop);
        foreach (var stream in resources.StreamNodes) Capture(stream.RequestStop);

        await CaptureAsync(TaskRunner.StopAsync).ConfigureAwait(false);
        await CaptureAsync(MessageFlowObservers.DisposeAsync).ConfigureAwait(false);

        foreach (var stream in resources.StreamNodes)
            await CaptureAsync(() => DisposeSafelyAsync(stream)).ConfigureAwait(false);

        // A STREAM node's native session service is created from the shared
        // MeshNode. Destroy the dependent session service and socket before
        // destroying the MeshNode that owns their routing plane.
        foreach (var node in resources.SpotNodes)
            await CaptureAsync(() => DisposeSafelyAsync(node)).ConfigureAwait(false);

        foreach (var bundle in resources.PublisherBundles)
            await CaptureAsync(() => DisposeSafelyAsync(bundle)).ConfigureAwait(false);

        foreach (var bundle in resources.SubscriberBundles)
            await CaptureAsync(() => DisposeSafelyAsync(bundle)).ConfigureAwait(false);

        await CaptureAsync(() => WaitForListenerTasksAsync(resources.ListenerTasks)).ConfigureAwait(false);

        Capture(ErrorSink.Dispose);
        Capture(StopTokenSource.Dispose);
        await CaptureAsync(() => DisposeSafelyAsync(Context)).ConfigureAwait(false);

        if (failures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures.Count > 1) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        void Capture(Action cleanup)
        {
            try
            {
                cleanup();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    private static async ValueTask WaitForListenerTasksAsync(Task[] listenerTasks)
    {
        if (listenerTasks.Length == 0) return;

        try
        {
            await Task.WhenAll(listenerTasks);
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException)
        {
        }
        catch (ZlinkCloseException)
        {
        }
    }

    private sealed record RuntimeResources(
        ZLinkSpotNodeRuntime[] SpotNodes,
        ZLinkStreamNodeRuntime[] StreamNodes,
        ZLinkChannelRuntimeBundle[] PublisherBundles,
        ZLinkChannelRuntimeBundle[] SubscriberBundles,
        Task[] ListenerTasks);

    private static async ValueTask DisposeSafelyAsync(IAsyncDisposable disposable)
    {
        try
        {
            await disposable.DisposeAsync();
        }
        catch (ObjectDisposedException)
        {
        }
        catch (ZlinkCloseException)
        {
        }
    }
}
