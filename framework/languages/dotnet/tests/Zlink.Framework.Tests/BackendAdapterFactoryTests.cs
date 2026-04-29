using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Tests;

public sealed class BackendAdapterFactoryTests
{
    [Fact]
    public async Task BackendFactory_Creates_Channel_Registry_Spot_And_Stream_Wrappers()
    {
        var factory = new ZLinkDotNetBackendAdapterFactory();
        var channelAdapter = factory.CreateChannelAdapter();
        var registryAdapter = factory.CreateRegistryAdapter();
        var spotAdapter = factory.CreateSpotAdapter();
        var streamAdapter = factory.CreateStreamAdapter();

        await using var context = channelAdapter.CreateContext();
        await using var discovery = channelAdapter.CreateDiscovery(
            context,
            ZLinkBackendServiceType.Socket,
            "profile");
        await using var dealer = channelAdapter.CreateDealerSocket(context);
        await using var router = channelAdapter.CreateRouterSocket(context);
        await using var publisher = channelAdapter.CreatePublisherSocket(context);
        await using var subscriber = channelAdapter.CreateSubscriberSocket(context);
        await using var registry = registryAdapter.CreateRegistry(context);
        await using var registryQueryClient = registryAdapter.CreateRegistryQueryClient(context);
        await using var spotNode = spotAdapter.CreateSpotNode(context);
        await using var streamSocket = streamAdapter.CreateStreamSocket(context);

        Assert.IsType<global::Zlink.Context>(context.NativeInstance);
        Assert.IsType<global::Zlink.Discovery>(discovery.NativeInstance);
        Assert.IsType<global::Zlink.DealerSocket>(dealer.NativeInstance);
        Assert.IsType<global::Zlink.RouterSocket>(router.NativeInstance);
        Assert.IsType<global::Zlink.PubSocket>(publisher.NativeInstance);
        Assert.IsType<global::Zlink.SubSocket>(subscriber.NativeInstance);
        Assert.IsType<global::Zlink.Registry>(registry.NativeInstance);
        Assert.IsType<global::Zlink.RegistryQueryClient>(registryQueryClient.NativeInstance);
        Assert.IsType<global::Zlink.SpotNode>(spotNode.NativeInstance);
        Assert.IsType<global::Zlink.StreamSocket>(streamSocket.NativeInstance);
    }

    [Fact]
    public void BackendFactory_Creates_MonitoringAdapter()
    {
        var factory = new ZLinkDotNetBackendAdapterFactory();

        Assert.NotNull(factory.CreateMonitoringAdapter());
    }
}
