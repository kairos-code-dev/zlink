namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkRuntimeState : IAsyncDisposable
{
    public ZLinkFrameworkRuntimeState(
        IZLinkBackendContext context,
        ZLinkFrameworkRegistration registration,
        IServiceProvider services,
        object executionOwner)
    {
        Context = context;
        Registration = registration;
        TaskRunner = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
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

    public ZLinkMessageFlowObserverPump MessageFlowObservers { get; }

    public Dictionary<string, ZLinkChannelRuntimeBundle> ServerBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> SubscriberBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> PublisherBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> ClientBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkRouteChannelRuntime> RouteChannels { get; } = new(StringComparer.Ordinal);

    public List<IZLinkBackendSpotRouteBridge> SpotRouteBridges { get; } = [];

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

    public async ValueTask DisposeAsync()
    {
        var failures = new List<Exception>();
        foreach (var node in SpotNodes.Values)
            await CaptureAsync(node.CloseLifecycleAsync).ConfigureAwait(false);

        Capture(StopTokenSource.Cancel);
        foreach (var node in SpotNodes.Values) Capture(node.RequestStop);
        foreach (var route in RouteChannels.Values) Capture(route.RequestStop);
        foreach (var stream in StreamNodes.Values) Capture(stream.RequestStop);

        await CaptureAsync(TaskRunner.StopAsync).ConfigureAwait(false);
        await CaptureAsync(MessageFlowObservers.DisposeAsync).ConfigureAwait(false);

        foreach (var node in SpotNodes.Values)
            await CaptureAsync(() => DisposeSafelyAsync(node)).ConfigureAwait(false);

        foreach (var routed in RouteChannels.Values)
            await CaptureAsync(() => DisposeSafelyAsync(routed)).ConfigureAwait(false);

        foreach (var stream in StreamNodes.Values)
            await CaptureAsync(() => DisposeSafelyAsync(stream)).ConfigureAwait(false);

        foreach (var bundle in ClientBundles.Values)
            await CaptureAsync(() => DisposeSafelyAsync(bundle)).ConfigureAwait(false);

        foreach (var bundle in PublisherBundles.Values)
            await CaptureAsync(() => DisposeSafelyAsync(bundle)).ConfigureAwait(false);

        foreach (var bundle in SubscriberBundles.Values)
            await CaptureAsync(() => DisposeSafelyAsync(bundle)).ConfigureAwait(false);

        foreach (var bundle in ServerBundles.Values)
            await CaptureAsync(() => DisposeSafelyAsync(bundle)).ConfigureAwait(false);

        await CaptureAsync(WaitForListenerTasksAsync).ConfigureAwait(false);

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

    private async ValueTask WaitForListenerTasksAsync()
    {
        if (ListenerTasks.Count == 0) return;

        try
        {
            await Task.WhenAll(ListenerTasks);
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
