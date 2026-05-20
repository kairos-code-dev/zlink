using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelConnectionManager(ZLinkFrameworkRuntime runtime) : IZLinkChannelConnectionManager
{
    public ValueTask<IZLinkEndpointConnections> GetClientAsync(
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return GetClientServerClientAsync(channelName, cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return GetFanoutSubscriberAsync(channelName, cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetClientServerClientAsync(
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetClientConnectionsAsync(channelName, cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetFanoutSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSubscriberConnectionsAsync(channelName, cancellationToken);
    }
}

internal sealed class ZLinkRuntimeConnections(
    Func<string, CancellationToken, ValueTask<bool>> connect,
    Func<string, CancellationToken, ValueTask> disconnect,
    Func<CancellationToken, ValueTask<IReadOnlyList<string>>> list)
    : IZLinkEndpointConnections
{
    public static ZLinkRuntimeConnections CreateManual<TBundle>(
        Func<TBundle> getBundle,
        Func<TBundle, IZLinkBackendConnectableSocket> getSocket,
        Func<TBundle, string, bool> tryAddManualConnection,
        Action<TBundle, string> removeManualConnection,
        Func<TBundle, IReadOnlyList<string>> listManualConnections)
    {
        return new ZLinkRuntimeConnections(
            (endpoint, _) =>
            {
                var bundle = getBundle();
                if (!tryAddManualConnection(bundle, endpoint))
                {
                    return ValueTask.FromResult(false);
                }

                getSocket(bundle).Connect(endpoint);
                return ValueTask.FromResult(true);
            },
            (endpoint, _) =>
            {
                var bundle = getBundle();
                getSocket(bundle).Disconnect(endpoint);
                removeManualConnection(bundle, endpoint);
                return ValueTask.CompletedTask;
            },
            _ => ValueTask.FromResult(listManualConnections(getBundle())));
    }

    public ValueTask<bool> ConnectAsync(
        string endpoint,
        CancellationToken cancellationToken = default)
    {
        return connect(endpoint, cancellationToken);
    }

    public ValueTask DisconnectAsync(
        string endpoint,
        CancellationToken cancellationToken = default)
    {
        return disconnect(endpoint, cancellationToken);
    }

    public ValueTask<IReadOnlyList<string>> ListConnectionsAsync(
        CancellationToken cancellationToken = default)
    {
        return list(cancellationToken);
    }
}
