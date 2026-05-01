using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeBundle : IAsyncDisposable
{
    private readonly HashSet<string> _manualConnections = new(StringComparer.Ordinal);

    public ZLinkChannelRuntimeBundle(
        IZLinkBackendSocket socket,
        ZLinkAsyncSubmitter? submitter = null)
    {
        Socket = socket;
        Submitter = submitter;
    }

    public IZLinkBackendSocket Socket { get; }

    public ZLinkAsyncSubmitter? Submitter { get; }

    public IZLinkBackendDiscovery? Discovery { get; set; }

    public bool TryAddManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Add(endpoint);
        }
    }

    public void RemoveManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            _manualConnections.Remove(endpoint);
        }
    }

    public bool ContainsManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Contains(endpoint);
        }
    }

    public IReadOnlyList<string> ListManualConnections()
    {
        lock (_manualConnections)
        {
            return _manualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Submitter is not null)
        {
            await Submitter.DisposeAsync();
        }

        if (Discovery is not null)
        {
            await Discovery.DisposeAsync();
        }

        await Socket.DisposeAsync();
    }
}
