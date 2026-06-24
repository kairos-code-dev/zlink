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
    ZLinkFrameworkRegistration frameworkRegistration,
    IZLinkBackendSpotNode node,
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

            connectDiscoveredPubSubPeers();

            var bundle = CreatePublisherBundle();

            _publisherBundles.Add(channelName, bundle);
            return bundle;
        }
    }

    private ZLinkSpotPublisherBundle CreatePublisherBundle()
    {
        var publisher = node.CreateSpot();
        return new ZLinkSpotPublisherBundle(
            publisher,
            new ZLinkAsyncSubmitter(
                publisher.OnSendReady,
                frameworkRegistration.DefaultSocketSendTimeout,
                stopToken));
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
