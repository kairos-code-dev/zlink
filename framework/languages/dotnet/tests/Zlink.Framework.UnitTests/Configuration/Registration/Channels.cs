using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class ChannelsTests : RegistrationValidationSupport
{
    [Fact]
    public void AddZLinkFramework_Throws_WhenChannelNameIsDuplicated()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                {
                    var channel = options.AddClientServerChannel("profile");
                    channel.EnableServer("tcp://127.0.0.1:7101");
                }
                {
                    var channel = options.AddClientServerChannel("profile");
                    channel.EnableClient();
                }
            }));

        Assert.Contains("Duplicate channel name", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddChannel_And_AddRouteChannel_Are_Removed_From_Public_Surface()
    {
        Assert.Null(typeof(IZLinkFrameworkOptions).GetMethod("AddChannel"));
        Assert.Null(typeof(IZLinkFrameworkOptions).GetMethod("AddRouteChannel"));
    }

    [Fact]
    public void LocationAutoConnect_DoesNotConflict_WithManualRouteMeshClient()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMeshChannel("play")
                .EnableServer("tcp://127.0.0.1:7101")
                .EnableClient("tcp://127.0.0.1:7102");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        var route = Assert.Single(registration.RouteChannels.Values);
        Assert.Equal(["tcp://127.0.0.1:7102"], route.ManualConnections.ListConnections());
        Assert.Equal(ZLinkPeerAcquisitionMode.Manual, route.AcquisitionMode);
    }

    [Fact]
    public void ConfigureDispatch_Exposes_Unhandled_And_Diagnostics_Defaults()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var dispatch = options.ConfigureDispatch();
                Assert.Equal(ZLinkUnhandledDispatchAction.ReplyError, dispatch.Unhandled.Request);
                Assert.Equal(ZLinkUnhandledDispatchAction.LogAndDrop, dispatch.Unhandled.Send);
                Assert.Equal(ZLinkUnhandledDispatchAction.LogAndDrop, dispatch.Unhandled.Publish);
                Assert.Equal(ZLinkMessageFlowLogMode.ErrorsOnly, dispatch.Diagnostics.MessageFlow);
                Assert.Equal(1.0d, dispatch.Diagnostics.SampleRate);
                Assert.True(dispatch.Diagnostics.IncludeMessageSizes);
                Assert.False(dispatch.Diagnostics.IncludeNativeDiagnostics);
            }
        });
        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();

        Assert.Equal(ZLinkUnhandledDispatchAction.ReplyError, registration.DispatchOptions.Unhandled.Request);
    }

    [Fact]
    public void ConfigureDispatch_Rejects_ReplyError_For_Send()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                {
                    var dispatch = options.ConfigureDispatch();
                    dispatch.Unhandled.Send = ZLinkUnhandledDispatchAction.ReplyError;
                }
            }));

        Assert.Contains("send dispatch cannot use ReplyError", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ConfigureDispatch_Rejects_ReplyError_For_Publish()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                {
                    var dispatch = options.ConfigureDispatch();
                    dispatch.Unhandled.Publish = ZLinkUnhandledDispatchAction.ReplyError;
                }
            }));

        Assert.Contains("publish dispatch cannot use ReplyError", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ConfigureDispatch_Rejects_Invalid_Diagnostics_SampleRate()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                {
                    var dispatch = options.ConfigureDispatch();
                    dispatch.TraceSampleRate(1.1d);
                }
            }));

        Assert.Contains("SampleRate must be between", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsChannelClientManualConnections_WhenLocationAutoConnectIsConfigured()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("profile").EnableClient("tcp://127.0.0.1:7101");
            }

            options.UseTestLocationStore();
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Equal(
            ZLinkPeerAcquisitionMode.Manual,
            Assert.Single(registration.Channels.Values).Client!.AcquisitionMode);
    }

    [Fact]
    public void AutoConnect_Role_Rejects_Runtime_Manual_Connection_Mutations()
    {
        var services = new ServiceCollection();
        IZLinkEndpointConnections? connections = null;
        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            connections = options.AddClientServerChannel("profile")
                .EnableClient()
                .ClientConnections;
        });

        Assert.NotNull(connections);
        Assert.Throws<InvalidOperationException>(() =>
            connections.Connect("tcp://127.0.0.1:7101"));
    }

    [Fact]
    public void AddZLinkFramework_AllowsRouteChannelManualConnections_WhenLocationAutoConnectIsConfigured()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var routed = options.AddRouteMeshChannel("backend");
                routed.EnableServer("tcp://127.0.0.1:7201");
                routed.EnableClient("tcp://127.0.0.1:7202");
            }
        });
    }

    [Fact]
    public void AddZLinkFramework_AllowsRouteChannelClientOnly_WhenLocationAutoConnectIsConfigured()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMeshChannel("backend")
                .EnableClient();
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        var route = Assert.Single(registration.RouteChannels.Values);
        Assert.Null(route.BindEndpoint);
        Assert.True(route.ClientEnabled);
        Assert.Equal(ZLinkPeerAcquisitionMode.AutoConnect, route.AcquisitionMode);
    }

    [Fact]
    public void AddZLinkFramework_AllowsRouteChannelClientOnly_WithManualConnection()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddRouteMeshChannel("backend")
                .EnableClient("tcp://127.0.0.1:7202");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        var route = Assert.Single(registration.RouteChannels.Values);
        Assert.Null(route.BindEndpoint);
        Assert.True(route.ClientEnabled);
        Assert.Equal(["tcp://127.0.0.1:7202"], route.ManualConnections.ListConnections());
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenRouteChannelHasNoCapability()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options => { options.AddRouteMeshChannel("backend"); }));

        Assert.Contains("must enable server or client capability", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenRouteChannelClientHasNoPeerAcquisitionPath()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("backend")
                    .EnableClient();
            }));

        Assert.Contains("requires location auto connect or manual connections", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsImplicitRouteMeshBridge_WhenLocationAutoConnectIsConfigured()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var routed = options.AddRouteMeshChannel("backend");
                routed.EnableServer("tcp://127.0.0.1:7203");
                routed.EnableClient("tcp://127.0.0.1:7204");
            }
            {
                var mesh = options.AddRouteMesh("spot.mesh");
                mesh.ChannelName("spot.mesh");
                {
                    var node = mesh;
                    {
                        var router = node.Listen("tcp://127.0.0.1:9105");
                    }
                }
            }
        });
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenClientHasNoPeerAcquisitionPath()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options => { options.AddClientServerChannel("profile").EnableClient(); }));

        Assert.Contains("requires location auto connect or manual connections", exception.Message, StringComparison.Ordinal);
    }
}
