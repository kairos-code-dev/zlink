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

        var attached = RequireAttachedChannelClient(channelName);
        var bundle = CreateAttachedChannelBundle(attached);

        _channelBundles.Add(channelName, bundle);
        return bundle;
    }

    public ZLinkSpotPublisherBundle GetOrCreatePublisherBundle(string channelName)
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

    private ZLinkSpotChannelClientRegistration RequireAttachedChannelClient(string channelName)
    {
        return registration.AttachedChannelClients.TryGetValue(channelName, out var attached)
            ? attached
            : throw new ZLinkConfigurationException(
                $"SPOT node '{nodeName}' attached channel client '{channelName}' is not registered.");
    }

    private ZLinkSpotPublisherClientRegistration RequireAttachedSpotPublisherClient(string channelName)
    {
        return registration.AttachedSpotPublisherClients.TryGetValue(channelName, out var attached)
            ? attached
            : throw new ZLinkConfigurationException(
                $"SPOT node '{nodeName}' publisher client '{channelName}' is not registered.");
    }

    private ZLinkSpotAttachedChannelBundle CreateAttachedChannelBundle(
        ZLinkSpotChannelClientRegistration attached)
    {
        var dealer = channelAdapter.CreateDealerSocket(context);
        dealer.SetChannelName(attached.ChannelName);
        var bundle = new ZLinkSpotAttachedChannelBundle(dealer);

        if (attached.ManualConnections.Count > 0)
        {
            AttachManualChannelDealer(attached, bundle);
        }
        else
        {
            AttachDiscoveredChannelDealer(attached, bundle);
        }

        return bundle;
    }

    private void AttachManualChannelDealer(
        ZLinkSpotChannelClientRegistration attached,
        ZLinkSpotAttachedChannelBundle bundle)
    {
        foreach (var endpoint in attached.ManualConnections)
        {
            bundle.Socket.Connect(endpoint);
            _ = bundle.TryAddManualConnection(endpoint);
        }

        node.AttachChannelDealerManual(attached.ChannelName, bundle.Socket);
    }

    private void AttachDiscoveredChannelDealer(
        ZLinkSpotChannelClientRegistration attached,
        ZLinkSpotAttachedChannelBundle bundle)
    {
        var discovery = ZLinkBackendDiscoveryFactory.Create(
            channelAdapter,
            context,
            attached.ChannelName,
            ZLinkAutoConnectType.ClientServer,
            frameworkRegistration.Discovery?.Endpoints ?? []);

        node.AttachChannelDealer(discovery, bundle.Socket);
        bundle.Discovery = discovery;
    }

    private ZLinkSpotPublisherBundle CreatePublisherBundle(
        ZLinkSpotPublisherClientRegistration attached)
    {
        var publisher = node.CreateSpot();
        return new ZLinkSpotPublisherBundle(
            publisher,
            new ZLinkAsyncSubmitter(
                publisher.OnSendReady,
                attached.SocketConfig.SendTimeout,
                stopToken));
    }

    private void ConnectPublisherManualPeers(ZLinkSpotPublisherClientRegistration attached)
    {
        foreach (var endpoint in attached.ManualConnections)
        {
            if (peerConnections.TryAddPubSubManual(endpoint))
            {
                node.ConnectPeer(endpoint);
            }
        }
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
