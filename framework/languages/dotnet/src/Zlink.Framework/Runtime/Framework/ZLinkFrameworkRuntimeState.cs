using Zlink.Framework.Backend;

namespace Zlink.Framework.Runtime.Framework;

internal sealed class ZLinkFrameworkRuntimeState(
    IZLinkBackendContext context,
    ZLinkFrameworkRegistration registration) : IAsyncDisposable
{
    public IZLinkBackendContext Context { get; } = context;

    public ZLinkFrameworkRegistration Registration { get; } = registration;

    public object SyncRoot { get; } = new();

    public CancellationTokenSource StopTokenSource { get; } = new();

    public Dictionary<string, ZLinkChannelRuntimeBundle> ServerBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> SubscriberBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> PublisherBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkChannelRuntimeBundle> ClientBundles { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotNodeRuntime> SpotNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, IZLinkBackendDiscovery> SpotDiscoveries { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkStreamNodeRuntime> StreamNodes { get; } = new(StringComparer.Ordinal);

    public List<Task> ListenerTasks { get; } = [];

    public async ValueTask DisposeAsync()
    {
        StopTokenSource.Cancel();

        foreach (var bundle in ClientBundles.Values)
        {
            await DisposeSafelyAsync(bundle);
        }

        foreach (var bundle in PublisherBundles.Values)
        {
            await DisposeSafelyAsync(bundle);
        }

        foreach (var bundle in SubscriberBundles.Values)
        {
            await DisposeSafelyAsync(bundle);
        }

        foreach (var bundle in ServerBundles.Values)
        {
            await DisposeSafelyAsync(bundle);
        }

        foreach (var node in SpotNodes.Values)
        {
            await DisposeSafelyAsync(node);
        }

        foreach (var stream in StreamNodes.Values)
        {
            await DisposeSafelyAsync(stream);
        }

        foreach (var discovery in SpotDiscoveries.Values)
        {
            await DisposeSafelyAsync(discovery);
        }

        StopTokenSource.Dispose();
        await DisposeSafelyAsync(Context);
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
        catch (global::Zlink.ZlinkCloseException)
        {
        }
    }
}
