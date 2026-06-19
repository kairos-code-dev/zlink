using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
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
    public void GlobalDiscovery_DoesNotConflict_WithManualRouteMeshClient()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
            options.AddRouteMeshChannel("play")
                .EnableServer("tcp://127.0.0.1:7101")
                .EnableClient("tcp://127.0.0.1:7102");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        var route = Assert.Single(registration.RouteChannels.Values);
        Assert.Equal(["tcp://127.0.0.1:7102"], route.ManualConnections);
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
                    dispatch.Diagnostics.SampleRate = 1.1d;

                }
            }));

        Assert.Contains("SampleRate must be between", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RejectsFanoutChannel()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                {
                    var channel = options.AddFanoutChannel("events");
                    channel.EnablePublisher("tcp://127.0.0.1:7102");

                }
                {
                    var mesh = options.AddSpotMesh("spot.mesh");
                    mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                    {
                        var node = mesh.AddNode("stage-node");
                        {
                            var router = node.EnableRouter("tcp://127.0.0.1:9100");

                        }
                                                node.AcceptSpotRoutesFromChannel("events", "tcp://127.0.0.1:7102");

                    }

                }
            }));

        Assert.Contains("must be a client/server channel or a route mesh channel", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RejectsDealerMeshChannel()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                                options.AddDealerMeshChannel("mesh").EnableClient();
                {
                    var mesh = options.AddSpotMesh("spot.mesh");
                    mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                    {
                        var node = mesh.AddNode("stage-node");
                        {
                            var router = node.EnableRouter("tcp://127.0.0.1:9101");

                        }
                                                node.AcceptSpotRoutesFromChannel("mesh", "tcp://127.0.0.1:7103");

                    }

                }
            }));

        Assert.Contains("must be a client/server channel or a route mesh channel", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RejectsAmbiguousChannelName()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                                options.AddClientServerChannel("route").EnableClient();
                                options.AddRouteMeshChannel("route").EnableServer("tcp://127.0.0.1:7104");
                {
                    var mesh = options.AddSpotMesh("spot.mesh");
                    mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                    {
                        var node = mesh.AddNode("stage-node");
                        {
                            var router = node.EnableRouter("tcp://127.0.0.1:9102");

                        }
                                                node.AcceptSpotRoutesFromChannel("route", "tcp://127.0.0.1:7104");

                    }

                }
            }));

        Assert.Contains("ambiguous", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RejectsUnknownChannel()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                {
                    var mesh = options.AddSpotMesh("spot.mesh");
                    {
                        var node = mesh.AddNode("stage-node");
                    {
                        var router = node.EnableRouter("tcp://127.0.0.1:9103");

                    }
                                        node.AcceptSpotRoutesFromChannel("missing", "tcp://127.0.0.1:7104");

                    }

                }
            }));

        Assert.Contains("is not registered", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RequiresEnableRouter()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                                options.AddClientServerChannel("api").EnableServer("tcp://127.0.0.1:7105");
                {
                    var mesh = options.AddSpotMesh("spot.mesh");
                    mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                    {
                        var node = mesh.AddNode("stage-node");
                                                node.AcceptSpotRoutesFromChannel("api", "tcp://127.0.0.1:7105");

                    }

                }
            }));

        Assert.Contains("must enable router", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RequiresDiscoveryOrManualConnections()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                                options.AddClientServerChannel("api").EnableServer("tcp://127.0.0.1:7106");
                {
                    var mesh = options.AddSpotMesh("spot.mesh");
                    mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                    {
                        var node = mesh.AddNode("stage-node");
                        {
                            var router = node.EnableRouter("tcp://127.0.0.1:9104");

                        }
                        node.AcceptSpotRoutesFromChannel("api");

                    }

                }
            }));

        Assert.Contains("requires discovery or manual connections", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsChannelClientManualConnections_WhenDiscoveryIsConfigured()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("profile").EnableClient("tcp://127.0.0.1:7101");

            }

            options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
        });
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenRouteChannelMixesDiscoveryAndManualConnections()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                {
                    var routed = options.AddRouteMeshChannel("backend");
                    routed.EnableServer("tcp://127.0.0.1:7201");
                    routed.EnableClient("tcp://127.0.0.1:7202");

                }
            }));

        Assert.Contains("cannot mix discovery and manual connections", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsAcceptedRouteChannelManualConnections_WhenDiscoveryIsConfigured()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
            {
                var routed = options.AddRouteMeshChannel("backend");
                routed.EnableServer("tcp://127.0.0.1:7203");
                routed.EnableClient("tcp://127.0.0.1:7204");

            }
            {
                var mesh = options.AddSpotMesh("spot.mesh");
                mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                {
                    var node = mesh.AddNode("stage-node");
                    {
                        var router = node.EnableRouter("tcp://127.0.0.1:9105");

                    }
                    node.AcceptSpotRoutesFromChannel("backend");

                }

            }
        });
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenClientHasNoPeerAcquisitionPath()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                                options.AddClientServerChannel("profile").EnableClient();
            }));

        Assert.Contains("requires discovery or manual connections", exception.Message, StringComparison.Ordinal);
    }

}
