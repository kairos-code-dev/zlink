using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests.Channels;
public sealed class HandlerClientsTests
{
    [Fact]
    public async Task ChannelHandler_Uses_IZLinkClient_To_Request_Another_Channel()
    {
        var backendEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var backendBuilder = Host.CreateApplicationBuilder();
        backendBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        backendBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            options.AddClientServerChannel("backend", channel =>
            {
                channel.EnableServer(server => server.Bind(backendEndpoint));
                channel.AddHandlerGroup("profile");
            });
        });

        var apiBuilder = Host.CreateApplicationBuilder();
        apiBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.AddHandlerGroup("profile-forward");
            });
            options.AddClientServerChannel("backend", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(backendEndpoint));
                });
            });
        });

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(apiEndpoint));
                });
            });
        });

        using var backendHost = backendBuilder.Build();
        using var apiHost = apiBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await backendHost.StartAsync();
        await apiHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client
                .RequestChannel("api", new ForwardProfileRequest { UserId = "forwarded" })
                .SubmitAsync<ProfileReply>(),
            static result => result.Name == "user:forwarded");

        Assert.Equal("user:forwarded", reply.Name);

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, apiHost, backendHost);
    }

    [Fact]
    public async Task ChannelHandler_Uses_IZLinkFanoutClient_To_Publish_Event()
    {
        var pubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<ProfileEventRecorder>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            options.AddFanoutChannel("profile", channel =>
            {
                channel.EnableSubscriber(subscriber =>
                {
                    subscriber.UseManualConnections(connections => connections.Connect(pubEndpoint));
                });
                channel.AddHandlerGroup("profile-events");
            });
        });

        var apiBuilder = Host.CreateApplicationBuilder();
        apiBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            options.AddFanoutChannel("profile", channel =>
            {
                channel.EnablePublisher(publisher => publisher.Bind(pubEndpoint));
            });
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.AddHandlerGroup("profile-publisher");
            });
        });

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(apiEndpoint));
                });
            });
        });

        using var subscriberHost = subscriberBuilder.Build();
        using var apiHost = apiBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await subscriberHost.StartAsync();
        await apiHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkChannelClient>();
        var recorder = subscriberHost.Services.GetRequiredService<ProfileEventRecorder>();
        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                var reply = await client
                    .RequestChannel("api", new PublishProfileRequest { UserId = "published" })
                    .SubmitAsync<PublishProfileReply>();
                Assert.True(reply.Accepted);
                await Task.Yield();
                return recorder.Events.Count;
            },
            count => count > 0);

        Assert.Contains("published", recorder.Events.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, apiHost, subscriberHost);
    }
}
