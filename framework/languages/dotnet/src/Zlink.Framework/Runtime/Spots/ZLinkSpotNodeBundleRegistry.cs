using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeBundleRegistry(
    string nodeName,
    ZLinkFrameworkRegistration frameworkRegistration,
    ZLinkSpotNodeRegistration registration,
    IZLinkBackendContext context,
    IZLinkChannelBackendAdapter channelAdapter,
    IZLinkBackendSpotNode node,
    ZLinkSpotPeerConnectionSet peerConnections,
    CancellationToken stopToken,
    Action connectDiscoveredPubSubPeers) : IAsyncDisposable
{
    private readonly Dictionary<string, ZLinkSpotAttachedChannelBundle> _channelBundles = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkSpotPublisherBundle> _publisherBundles = new(StringComparer.Ordinal);

    public IReadOnlyDictionary<string, ZLinkSpotAttachedChannelBundle> AttachedChannelBundles => _channelBundles;

    public IReadOnlyDictionary<string, ZLinkSpotPublisherBundle> PublisherBundles => _publisherBundles;

    public void AddChannelBundle(string channelName, ZLinkSpotAttachedChannelBundle bundle)
    {
        _channelBundles.Add(channelName, bundle);
    }

    public void AddPublisherBundle(string channelName, ZLinkSpotPublisherBundle bundle)
    {
        _publisherBundles.Add(channelName, bundle);
    }

    public ZLinkSpotAttachedChannelBundle GetOrCreateAttachedChannelBundle(string channelName)
    {
        if (_channelBundles.TryGetValue(channelName, out var existing))
        {
            return existing;
        }

        if (!registration.AttachedChannelClients.TryGetValue(channelName, out var attached))
        {
            throw new InvalidOperationException(
                $"SPOT node '{nodeName}' attached channel client '{channelName}' is not registered.");
        }

        var dealer = channelAdapter.CreateDealerSocket(context);
        dealer.SetChannelName(attached.ChannelName);
        var bundle = new ZLinkSpotAttachedChannelBundle(dealer);

        if (attached.ManualConnections.Count > 0)
        {
            foreach (var endpoint in attached.ManualConnections)
            {
                dealer.Connect(endpoint);
                _ = bundle.TryAddManualConnection(endpoint);
            }

            node.AttachChannelDealerManual(attached.ChannelName, dealer);
        }
        else
        {
            var discovery = ZLinkBackendDiscoveryFactory.Create(
                channelAdapter,
                context,
                attached.ChannelName,
                ZLinkAutoConnectType.ClientServer,
                frameworkRegistration.Discovery?.Endpoints ?? []);

            node.AttachChannelDealer(discovery, dealer);
            bundle.Discovery = discovery;
        }

        _channelBundles.Add(channelName, bundle);
        return bundle;
    }

    public ZLinkSpotPublisherBundle GetOrCreatePublisherBundle(string channelName)
    {
        if (_publisherBundles.TryGetValue(channelName, out var existing))
        {
            return existing;
        }

        if (!registration.AttachedSpotPublisherClients.TryGetValue(channelName, out var attached))
        {
            throw new InvalidOperationException(
                $"SPOT node '{nodeName}' publisher client '{channelName}' is not registered.");
        }

        connectDiscoveredPubSubPeers();

        var publisher = node.CreateSpot();
        var bundle = new ZLinkSpotPublisherBundle(
            publisher,
            new ZLinkAsyncSubmitter(
                publisher.OnSendReady,
                attached.SocketOptions.SendTimeout,
                stopToken));

        if (attached.ManualConnections.Count > 0)
        {
            foreach (var endpoint in attached.ManualConnections)
            {
                if (peerConnections.TryAddPubSubManual(endpoint))
                {
                    node.ConnectPeer(endpoint);
                }
            }
        }

        _publisherBundles.Add(channelName, bundle);
        return bundle;
    }

    public async ValueTask DisposeAsync()
    {
        foreach (var publisher in _publisherBundles.Values)
        {
            await publisher.DisposeAsync();
        }

        foreach (var channel in _channelBundles.Values)
        {
            await channel.DisposeAsync();
        }
    }
}
