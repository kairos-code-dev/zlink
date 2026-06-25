using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;


public sealed class RegistryRemoteAddressesTests : RegistrationValidationSupport
{
    [Fact]
    public void RegistrySpotRemoteAddresses_Registers_Default_Service()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
            {
                var mesh = options.AddSpotMesh("spot");
                mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                mesh.EnableRouter("tcp://127.0.0.1:6201");

            }
            {
                var routed = options.AddRouteMesh("play");
                routed.EnableServer("tcp://127.0.0.1:6202");

            }
            options.UseRegistrySpotRemoteAddresses("bingo").RouterChannelId = "play";
        });

        using var provider = services.BuildServiceProvider();
        Assert.IsType<ZLinkRegistrySpotRemoteAddressResolver>(
            provider.GetRequiredService<IZLinkSpotRemoteAddressResolver>());
    }

    [Fact]
    public void SpotMesh_UseRegistrySpotResolver_UsesMeshNamespaceAndFirstNodeRouter()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
            options.AddSpotMesh("rooms")
                .UseRegistrySpotResolver()
                .EnableRouter("tcp://127.0.0.1:6201");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Equal("rooms", registration.RegistrySpotRemoteAddresses?.Namespace);
        Assert.Equal("rooms", registration.RegistrySpotRemoteAddresses?.RouterChannelId);
    }

    [Fact]
    public void RegistrySpotRemoteAddresses_Require_SpotDiscovery()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                {
                    var routed = options.AddRouteMesh("play");
                    routed.EnableServer("tcp://127.0.0.1:6202");

                }
                options.UseRegistrySpotRemoteAddresses("bingo");
            }));

        Assert.Contains("requires discovery endpoints", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryRouteResolvers_Reject_Custom_Duplicate()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseRegistrySpotRemoteAddresses("bingo");
                options.AddSpotRemoteAddressResolver<TestSpotRemoteAddressResolver>();
            }));

        Assert.Contains("SPOT remote address resolver", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryRouteResolvers_Require_Explicit_RouterChannel_When_Ambiguous()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                {
                    var mesh = options.AddSpotMesh("spot");
                    mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                    mesh.EnableRouter("tcp://127.0.0.1:6201");

                }
                {
                    var routed = options.AddRouteMesh("play-a");
                    routed.EnableServer("tcp://127.0.0.1:6202");

                }
                {
                    var routed = options.AddRouteMesh("play-b");
                    routed.EnableServer("tcp://127.0.0.1:6203");

                }
                options.UseRegistrySpotRemoteAddresses("bingo");
            }));

        Assert.Contains("requires RouterChannelId", exception.Message, StringComparison.Ordinal);
    }

}
