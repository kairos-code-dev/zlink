using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class LifecycleHostedServiceTests
{
    [Fact]
    public async Task Host_Starts_And_Stops_FrameworkRuntimeContext()
    {
        var pubPort = GetEphemeralPort();
        var routerPort = GetEphemeralPort();
        var builder = Host.CreateApplicationBuilder();

        builder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add($"tcp://127.0.0.1:{routerPort}"));
            options.AddClientServerChannel("profile", channel => channel.EnableClient());
        });
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = $"tcp://127.0.0.1:{pubPort}";
            options.RouterEndpoint = $"tcp://127.0.0.1:{routerPort}";
        });

        using var host = builder.Build();
        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();

        Assert.Null(runtime.Context);

        await host.StartAsync();

        Assert.NotNull(runtime.Context);
        Assert.IsType<global::Systems.Zlink.Context>(runtime.Context!.NativeInstance);

        await host.StopAsync();

        Assert.Null(runtime.Context);
    }

    [Fact]
    public async Task Host_Starts_EmbeddedRegistry_Before_FrameworkRuntime()
    {
        var pubPort = GetEphemeralPort();
        var routerPort = GetEphemeralPort();
        var builder = Host.CreateApplicationBuilder();

        builder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add($"tcp://127.0.0.1:{routerPort}");
            });
            options.AddClientServerChannel("profile", channel => channel.EnableClient());
        });
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = $"tcp://127.0.0.1:{pubPort}";
            options.RouterEndpoint = $"tcp://127.0.0.1:{routerPort}";
        });

        using var host = builder.Build();
        var registryRuntime = host.Services.GetRequiredService<ZLinkRegistryRuntime>();
        var frameworkRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();

        await host.StartAsync();

        Assert.True(registryRuntime.IsStarted);
        Assert.True(frameworkRuntime.IsStarted);
        Assert.NotNull(registryRuntime.Context);
        Assert.NotNull(registryRuntime.Registry);
        Assert.NotNull(frameworkRuntime.Context);

        await host.StopAsync();

        Assert.False(registryRuntime.IsStarted);
        Assert.False(frameworkRuntime.IsStarted);
    }

    private static int GetEphemeralPort()
    {
        using var listener = new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback, 0);
        listener.Start();
        return ((System.Net.IPEndPoint)listener.LocalEndpoint).Port;
    }
}
