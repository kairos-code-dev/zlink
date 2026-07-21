using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class ChannelsTests : RegistrationValidationSupport
{
    [Fact]
    public void AddZLinkFramework_Throws_WhenMeshNameIsDuplicated()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMesh("profile");
                options.AddRouteMesh("profile");
            }));

        Assert.Contains("Duplicate RouteMesh name", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RemovedTopologyRegistrationMethods_AreAbsent()
    {
        Assert.Null(typeof(IZLinkFrameworkOptions).GetMethod("AddChannel"));
        Assert.Null(typeof(IZLinkFrameworkOptions).GetMethod("AddRouteChannel"));
    }

    [Fact]
    public void MeshPeerConnections_RecordManualPeers()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            var mesh = options.AddRouteMesh("play")
                .Listen("tcp://127.0.0.1:7101")
                .SetRoutingId(RoutingId.From("play"));
            mesh.ChannelName("play");
            mesh.PeerConnections.Connect(
                RoutingId.From("peer"), "tcp://127.0.0.1:7102");
        });

        var registration = services.BuildServiceProvider()
            .GetRequiredService<ZLinkFrameworkRegistration>();
        var node = Assert.Single(registration.SpotNodes.Values);
        var endpoint = Assert.Single(node.Router!.ManualConnections.ListConnections());
        Assert.Equal("tcp://127.0.0.1:7102", endpoint);
        Assert.Equal(RoutingId.From("peer"), node.Router.PeerRoutingIds[endpoint]);
    }

    [Fact]
    public void ConfigureDispatch_RejectsInvalidPolicies()
    {
        var services = new ServiceCollection();

        var send = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
                options.ConfigureDispatch().Unhandled.Send = ZLinkUnhandledDispatchAction.ReplyError));
        Assert.Contains("send dispatch cannot use ReplyError", send.Message, StringComparison.Ordinal);

        var sampleRate = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
                options.ConfigureDispatch().TraceSampleRate(1.1d)));
        Assert.Contains("SampleRate must be between", sampleRate.Message, StringComparison.Ordinal);
    }
}
