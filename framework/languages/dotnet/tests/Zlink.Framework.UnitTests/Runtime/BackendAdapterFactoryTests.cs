using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests;

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
            ZLinkAutoConnectType.ClientServer,
            "profile");
        await using var dealer = channelAdapter.CreateDealerSocket(context);
        await using var router = channelAdapter.CreateRouterSocket(context);
        await using var publisher = channelAdapter.CreatePublisherSocket(context);
        await using var subscriber = channelAdapter.CreateSubscriberSocket(context);

        Assert.IsAssignableFrom<IContext>(context.NativeInstance);
        Assert.IsAssignableFrom<IDiscovery>(discovery.NativeInstance);
        Assert.IsAssignableFrom<IDealerSocket>(dealer.NativeInstance);
        Assert.IsAssignableFrom<IRouterSocket>(router.NativeInstance);
        Assert.IsAssignableFrom<IPubSocket>(publisher.NativeInstance);
        Assert.IsAssignableFrom<ISubSocket>(subscriber.NativeInstance);
        await AssertRegistryBackendAsync(channelAdapter, registryAdapter);
        await AssertSpotBackendAsync(channelAdapter, spotAdapter);
        await AssertStreamBackendAsync(channelAdapter, streamAdapter);
    }

    private static async Task AssertRegistryBackendAsync(
        IZLinkChannelBackendAdapter channelAdapter,
        IZLinkRegistryBackendAdapter registryAdapter)
    {
        await using var context = channelAdapter.CreateContext();
        await using var registry = registryAdapter.CreateRegistry(context);
        await using var registryQueryClient = registryAdapter.CreateRegistryQueryClient(context);

        Assert.IsAssignableFrom<IRegistry>(registry.NativeInstance);
        Assert.IsAssignableFrom<IRegistryQueryClient>(registryQueryClient.NativeInstance);
    }

    private static async Task AssertSpotBackendAsync(
        IZLinkChannelBackendAdapter channelAdapter,
        IZLinkSpotBackendAdapter spotAdapter)
    {
        await using var context = channelAdapter.CreateContext();
        await using var spotNode = spotAdapter.CreateSpotNode(context, SpotNodeMode.All);

        Assert.IsAssignableFrom<ISpotNode>(spotNode.NativeInstance);
    }

    private static async Task AssertStreamBackendAsync(
        IZLinkChannelBackendAdapter channelAdapter,
        IZLinkStreamBackendAdapter streamAdapter)
    {
        await using var context = channelAdapter.CreateContext();
        await using var streamSocket = streamAdapter.CreateStreamSocket(context);

        Assert.IsAssignableFrom<IStreamSocket>(streamSocket.NativeInstance);
    }

    [Fact]
    public void BackendFactory_Creates_MonitoringAdapter()
    {
        var factory = new ZLinkDotNetBackendAdapterFactory();

        Assert.NotNull(factory.CreateMonitoringAdapter());
    }
}
