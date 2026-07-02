namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkRuntimeState : IAsyncDisposable
{
    public ZLinkFrameworkRuntimeState(
        IZLinkBackendContext context,
        ZLinkFrameworkRegistration registration)
    {
        Context = context;
        Registration = registration;
        TaskRunner = new ZLinkRuntimeTaskRunner(
            new ZLinkRuntimeErrorSink(),
            StopTokenSource.Token);
    }

    public IZLinkBackendContext Context { get; }

    public ZLinkFrameworkRegistration Registration { get; }

    public object SyncRoot { get; } = new();

    public CancellationTokenSource StopTokenSource { get; } = new();

    public ZLinkRuntimeTaskRunner TaskRunner { get; }

    public Dictionary<string, ZLinkChannelRuntimeBundle> ServerBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> SubscriberBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> PublisherBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> ClientBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkRouteChannelRuntime> RouteChannels { get; } = new(StringComparer.Ordinal);

    public List<IZLinkBackendSpotRouteBridge> SpotRouteBridges { get; } = [];

    public Dictionary<string, ZLinkSpotNodeRuntime> SpotRouteBridgeOwners { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotNodeRuntime> SpotNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkStreamNodeRuntime> StreamNodes { get; } = new(StringComparer.Ordinal);

    public List<Task> ListenerTasks { get; } = [];

    public async ValueTask DisposeAsync()
    {
        StopTokenSource.Cancel();

        foreach (var node in SpotNodes.Values) await DisposeSafelyAsync(node);

        foreach (var routed in RouteChannels.Values) await DisposeSafelyAsync(routed);

        foreach (var stream in StreamNodes.Values) await DisposeSafelyAsync(stream);

        foreach (var bundle in ClientBundles.Values) await DisposeSafelyAsync(bundle);

        foreach (var bundle in PublisherBundles.Values) await DisposeSafelyAsync(bundle);

        foreach (var bundle in SubscriberBundles.Values) await DisposeSafelyAsync(bundle);

        foreach (var bundle in ServerBundles.Values) await DisposeSafelyAsync(bundle);

        await WaitForListenerTasksAsync();

        StopTokenSource.Dispose();
        await DisposeSafelyAsync(Context);
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
