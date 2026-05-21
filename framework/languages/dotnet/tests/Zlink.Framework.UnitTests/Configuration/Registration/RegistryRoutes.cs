using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;


public sealed class RegistryRoutesTests : RegistrationValidationSupport
{
    [Fact]
    public void RegistryActorRoutes_Registers_Default_Service()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.UseSpotDiscovery(
                "spot",
                discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind("tcp://127.0.0.1:6202");
            });
            options.UseRegistryActorRoutes("bingo");
        });

        using var provider = services.BuildServiceProvider();
        Assert.IsType<ZLinkRegistryActorRouteResolver>(
            provider.GetRequiredService<IZLinkActorPlayRouteResolver>());
    }

    [Fact]
    public void RegistrySpotRoutes_Registers_Default_Service()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.UseSpotDiscovery(
                "spot",
                discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind("tcp://127.0.0.1:6202");
            });
            options.UseRegistrySpotRoutes("bingo");
        });

        using var provider = services.BuildServiceProvider();
        Assert.IsType<ZLinkRegistrySpotRouteResolver>(
            provider.GetRequiredService<IZLinkSpotRouteResolver>());
    }

    [Fact]
    public void RegistrySpotRoutes_Require_SpotDiscovery()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("play", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6202");
                });
                options.UseRegistrySpotRoutes("bingo");
            }));

        Assert.Contains("requires AddSpotMesh", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryActorRoutes_Require_Discovery()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("play", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6202");
                });
                options.UseRegistryActorRoutes("bingo");
            }));

        Assert.Contains("requires UseDiscovery", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryRouteResolvers_Reject_Custom_Duplicate()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseRegistryActorRoutes("bingo");
                options.AddActorPlayRouteResolver<TestActorPlayRouteResolver>();
            }));

        Assert.Contains("already configured", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryRouteResolvers_Require_Explicit_RouterChannel_When_Ambiguous()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                options.UseSpotDiscovery(
                    "spot",
                    discovery => discovery.Add("tcp://127.0.0.1:5551"));
                options.AddRouteMeshChannel("play-a", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6202");
                });
                options.AddRouteMeshChannel("play-b", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6203");
                });
                options.UseRegistrySpotRoutes("bingo");
            }));

        Assert.Contains("requires RouterChannelId", exception.Message, StringComparison.Ordinal);
    }

}
