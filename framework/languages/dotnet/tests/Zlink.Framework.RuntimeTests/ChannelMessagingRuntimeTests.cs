using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class ChannelMessagingRuntimeTests
{
    [Fact]
    public async Task DiscoveryClient_Request_And_Send_Work_Across_Hosts()
    {
        var registryPubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.AddChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
            });
        });
        serverBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingRuntimeTests>();

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.AddChannel("api", channel =>
            {
                channel.EnableClient();
            });
        });

        using var registryHost = registryBuilder.Build();
        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await registryHost.StartAsync();
        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var recorder = serverHost.Services.GetRequiredService<ProfileCommandRecorder>();

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.Request("api", new GetProfileRequest { UserId = "discovery" }).ExecAsync(),
            static result => result.Name == "user:discovery");

        Assert.Equal("user:discovery", reply.Name);

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                client.Send("api", new RefreshProfileCacheCommand { UserId = "discovery" }).Exec();
                await Task.Yield();
                return recorder.Commands.Count;
            },
            count => count > 0);

        Assert.Contains("discovery", recorder.Commands.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost, registryHost);
    }
}
