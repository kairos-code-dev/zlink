using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Streams;
using Zlink.Framework.Tests.Common;

namespace Zlink.Framework.E2ETests.MultiProcess;

[CollectionDefinition(nameof(MultiProcessTestsCollection), DisableParallelization = true)]
public sealed class MultiProcessTestsCollection
{
}

[Collection(nameof(MultiProcessTestsCollection))]
public sealed partial class TopologyTests
{
    [Fact]
    public async Task RemoteRegistryQueryClient_Reads_FrameworkTopology_From_TestHostProcesses()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var channelEndpoint = GetFreeTcpEndpoint();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);
        await using var frameworkHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-server",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--server-endpoint", channelEndpoint);

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddZLinkRegistryQueryClient(options =>
        {
            options.Endpoint = registryRouterEndpoint;
        });

        using var host = builder.Build();
        await host.StartAsync(timeout.Token);

        var client = host.Services.GetRequiredService<IZLinkRegistryQueryClient>();
        var snapshot = await RetryAsync(
            () => client.SnapshotAsync().AsTask(),
            entries => entries.Any(entry =>
                entry.ChannelName == "profile"
                && entry.Endpoint == channelEndpoint
                && entry.ServiceRole == ZLinkServiceRole.Router),
            TimeSpan.FromSeconds(15));

        Assert.Contains(snapshot, entry => entry.ChannelName == "profile");

        await host.StopAsync(timeout.Token);
    }

    [Fact]
    public async Task RegistryMonitoring_Emits_Topology_And_Summary_For_Remote_Framework_Process()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var channelEndpoint = GetFreeTcpEndpoint();

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<RegistryChangeProbe>();
        builder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkRegistryEvent>>(static provider =>
            provider.GetRequiredService<RegistryChangeProbe>());
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddRegistryEvents("registry", TimeSpan.FromMilliseconds(100));
        });

        using var host = builder.Build();
        await host.StartAsync();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var frameworkHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-server",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--server-endpoint", channelEndpoint);

        var probe = host.Services.GetRequiredService<RegistryChangeProbe>();
        await probe.WaitForTopologyAsync(TimeSpan.FromSeconds(15));
        await probe.WaitForSummaryAsync(TimeSpan.FromSeconds(15));

        await host.StopAsync();
    }
}
