using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests;

public sealed class BackendAdapterFactoryTests
{
    [Fact]
    public async Task BackendFactory_Creates_Channel_Spot_And_Stream_Wrappers()
    {
        var factory = new ZLinkDotNetBackendAdapterFactory();
        var channelAdapter = factory.CreateChannelAdapter();
        var spotAdapter = factory.CreateSpotAdapter();
        var streamAdapter = factory.CreateStreamAdapter();

        await using var context = channelAdapter.CreateContext();
        await using var dealer = channelAdapter.CreateDealerSocket(context);
        await using var router = channelAdapter.CreateRouterSocket(context);
        await using var publisher = channelAdapter.CreatePublisherSocket(context);
        await using var subscriber = channelAdapter.CreateSubscriberSocket(context);

        Assert.IsType<ZLinkBackendContextWrapper>(context);
        Assert.IsType<ZLinkBackendDealerSocketWrapper>(dealer);
        Assert.IsType<ZLinkBackendRouterSocketWrapper>(router);
        Assert.IsType<ZLinkBackendPublisherSocketWrapper>(publisher);
        Assert.IsType<ZLinkBackendSubscriberSocketWrapper>(subscriber);
        await using var completionPump = dealer.CreateRequestCompletionPump();
        await using var monitor = factory.CreateMonitoringAdapter().OpenSocketMonitor(dealer);
        await AssertSpotBackendAsync(channelAdapter, spotAdapter);
        await AssertStreamBackendAsync(channelAdapter, streamAdapter);
    }

    private static async Task AssertSpotBackendAsync(
        IZLinkChannelBackendAdapter channelAdapter,
        IZLinkSpotBackendAdapter spotAdapter)
    {
        await using var context = channelAdapter.CreateContext();
        await using var spotNode = spotAdapter.CreateSpotNode(context, SpotNodeMode.All);

        Assert.IsType<ZLinkBackendSpotNodeWrapper>(spotNode);
    }

    private static async Task AssertStreamBackendAsync(
        IZLinkChannelBackendAdapter channelAdapter,
        IZLinkStreamBackendAdapter streamAdapter)
    {
        await using var context = channelAdapter.CreateContext();
        await using var streamSocket = streamAdapter.CreateStreamSocket(context);

        Assert.IsType<ZLinkBackendStreamSocketWrapper>(streamSocket);
    }

    [Fact]
    public void BackendFactory_Creates_MonitoringAdapter()
    {
        var factory = new ZLinkDotNetBackendAdapterFactory();

        Assert.NotNull(factory.CreateMonitoringAdapter());
    }

    [Fact]
    public async Task SpotNode_EntrySpot_IsSingleton_UnderConcurrentFirstAccess()
    {
        var factory = new ZLinkDotNetBackendAdapterFactory();
        var channelAdapter = factory.CreateChannelAdapter();
        var spotAdapter = factory.CreateSpotAdapter();
        await using var context = channelAdapter.CreateContext();
        await using var spotNode = spotAdapter.CreateSpotNode(context, SpotNodeMode.All);

        var accesses = Enumerable.Range(0, 32)
            .Select(_ => Task.Run(spotNode.EntrySpot))
            .ToArray();
        var entrySpots = await Task.WhenAll(accesses);

        Assert.All(entrySpots, entrySpot => Assert.Same(entrySpots[0], entrySpot));
    }
}
