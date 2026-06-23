using System.Diagnostics.CodeAnalysis;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeBundleRegistry(
    string nodeName,
    ZLinkFrameworkRegistration frameworkRegistration,
    ZLinkSpotNodeRegistration registration,
    IZLinkBackendSpotNode node,
    ZLinkSpotPeerConnectionSet peerConnections,
    CancellationToken stopToken,
    Action connectDiscoveredPubSubPeers) : IAsyncDisposable
{
    private readonly object _gate = new();
    private readonly Dictionary<string, ZLinkSpotPublisherBundle> _publisherBundles = new(StringComparer.Ordinal);

    public void AddPublisherBundle(string channelName, ZLinkSpotPublisherBundle bundle)
    {
        lock (_gate)
        {
            _publisherBundles.Add(channelName, bundle);
        }
    }

    public bool TryGetPublisherBundle(
        string channelName,
        [NotNullWhen(true)] out ZLinkSpotPublisherBundle? bundle)
    {
        lock (_gate)
        {
            return _publisherBundles.TryGetValue(channelName, out bundle);
        }
    }

    public ZLinkSpotPublisherBundle GetOrCreatePublisherBundle(string channelName)
    {
        lock (_gate)
        {
            if (_publisherBundles.TryGetValue(channelName, out var existing))
            {
                return existing;
            }

            var attached = RequireAttachedSpotPublisherClient(channelName);

            connectDiscoveredPubSubPeers();

            var bundle = CreatePublisherBundle(attached);
            ConnectPublisherManualPeers(attached);

            _publisherBundles.Add(channelName, bundle);
            return bundle;
        }
    }

    private ZLinkSpotPublisherClientRegistration RequireAttachedSpotPublisherClient(string channelName)
    {
        return registration.AttachedSpotPublisherClients.TryGetValue(channelName, out var attached)
            ? attached
            : throw new ZLinkConfigurationException(
                $"SPOT node '{nodeName}' publisher client '{channelName}' is not registered.");
    }

    private ZLinkSpotPublisherBundle CreatePublisherBundle(
        ZLinkSpotPublisherClientRegistration attached)
    {
        var publisher = node.CreateSpot();
        return new ZLinkSpotPublisherBundle(
            publisher,
            new ZLinkAsyncSubmitter(
                publisher.OnSendReady,
                attached.SocketConfig.SendTimeout ?? frameworkRegistration.DefaultSocketSendTimeout,
                stopToken));
    }

    private void ConnectPublisherManualPeers(ZLinkSpotPublisherClientRegistration attached)
    {
        foreach (var endpoint in attached.ManualConnections)
        {
            if (peerConnections.TryAddPubSubManual(endpoint))
            {
                try
                {
                    node.ConnectPeer(endpoint);
                }
                catch (ZlinkConnectException error)
                    when (error.Result == ZlinkConnectException.ErrorCode.Busy)
                {
                }
            }
        }
    }

    public async ValueTask DisposeAsync()
    {
        ZLinkSpotPublisherBundle[] publishers;
        lock (_gate)
        {
            publishers = _publisherBundles.Values.ToArray();
            _publisherBundles.Clear();
        }

        foreach (var publisher in publishers)
        {
            await publisher.DisposeAsync();
        }

    }
}
