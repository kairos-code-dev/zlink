using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

[CollectionDefinition(nameof(FrameworkRuntimeIntegrationTestsCollection), DisableParallelization = true)]
public sealed class FrameworkRuntimeIntegrationTestsCollection
{
}

[Collection(nameof(FrameworkRuntimeIntegrationTestsCollection))]
public sealed class ChannelMessagingIntegrationTests
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
        serverBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingIntegrationTests>();

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
            async () => await client.Request("api", new GetProfileRequest { UserId = "discovery" }).ExecAsync<ProfileReply>(),
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

    [Fact]
    public async Task ManualClient_Request_And_Send_Work_Across_Hosts()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
            });
        });
        serverBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingIntegrationTests>();

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(apiEndpoint));
                });
            });
        });

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var recorder = serverHost.Services.GetRequiredService<ProfileCommandRecorder>();

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.Request("api", new GetProfileRequest { UserId = "alice" }).ExecAsync<ProfileReply>(),
            static result => result.Name == "user:alice");

        Assert.Equal("user:alice", reply.Name);

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                client.Send("api", new RefreshProfileCacheCommand { UserId = "alice" }).Exec();
                await Task.Yield();
                return recorder.Commands.Count;
            },
            count => count > 0);

        Assert.Contains("alice", recorder.Commands.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
    }

    [Fact]
    public async Task ChannelConnectionManager_Connects_And_Lists_Endpoints_Async()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
            });
        });
        serverBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingIntegrationTests>();

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableClient();
            });
        });

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var connections = await clientHost.Services
            .GetRequiredService<IZLinkChannelConnectionManager>()
            .GetClientAsync("api");
        var client = clientHost.Services.GetRequiredService<IZLinkClient>();

        Assert.True(await connections.ConnectAsync(apiEndpoint));
        Assert.Contains(apiEndpoint, await connections.ListConnectionsAsync());

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.Request("api", new GetProfileRequest { UserId = "manager" }).ExecAsync<ProfileReply>(),
            static result => result.Name == "user:manager");

        Assert.Equal("user:manager", reply.Name);

        await connections.DisconnectAsync(apiEndpoint);
        Assert.DoesNotContain(apiEndpoint, await connections.ListConnectionsAsync());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
    }

    [Fact]
    public async Task Publisher_And_Subscriber_Work_Across_Hosts()
    {
        var pubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<ProfileEventRecorder>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("profile", channel =>
            {
                channel.EnableSubscriber(subscriber =>
                {
                    subscriber.UseManualConnections(connections => connections.Connect(pubEndpoint));
                });
            });
        });
        subscriberBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingIntegrationTests>();

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("profile", channel =>
            {
                channel.EnablePublisher(publisher => publisher.Bind(pubEndpoint));
            });
        });

        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var publisher = publisherHost.Services.GetRequiredService<IZLinkEventPublisher>();
        var recorder = subscriberHost.Services.GetRequiredService<ProfileEventRecorder>();

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                publisher.Publish(
                    "profile",
                    "profile.cache-invalidated",
                    new ProfileInvalidated { UserId = "alice" }).Exec();
                await Task.Yield();
                return recorder.Events.Count;
            },
            count => count > 0);

        Assert.Contains("alice", recorder.Events.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(publisherHost, subscriberHost);
    }

    [Fact]
    public async Task Filters_Run_In_Registration_Order_Around_Handler_Dispatch()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<FilterOrderRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseFilter<OuterOrderFilter>();
            options.UseFilter<InnerOrderFilter>();
            options.AddChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
            });
        });
        serverBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingIntegrationTests>();

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(apiEndpoint));
                });
            });
        });

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var recorder = serverHost.Services.GetRequiredService<FilterOrderRecorder>();

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.Request("api", new GetFilterOrderRequest()).ExecAsync<FilterOrderReply>(),
            static result => result.Sequence.Count == 5);

        Assert.Equal(
            ["outer:before", "inner:before", "handler", "inner:after", "outer:after"],
            reply.Sequence);
        Assert.Equal(reply.Sequence, recorder.Entries.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, serverHost);
    }

    [Fact]
    public async Task HttpHandler_Uses_SameServiceProvider_ToResolve_IZLinkClient()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var channelBuilder = Host.CreateApplicationBuilder();
        channelBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        channelBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
            });
        });
        channelBuilder.Services.AddZLinkHandlersFromAssemblyContaining<ChannelMessagingIntegrationTests>();

        var httpBuilder = Host.CreateApplicationBuilder();
        httpBuilder.Services.AddLogging();
        httpBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddChannel("api", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(apiEndpoint));
                });
            });
        });

        using var channelHost = channelBuilder.Build();
        using var httpHost = httpBuilder.Build();
        await channelHost.StartAsync();
        await httpHost.StartAsync();

        var handler = RequestDelegateFactory.Create(
            async (HttpContext context, [FromServices] IZLinkClient client, CancellationToken cancellationToken) =>
            {
                var userId = context.Request.Query["userId"].ToString();
                var reply = await client.Request("api", new GetProfileRequest { UserId = userId })
                    .ExecAsync<ProfileReply>(cancellationToken);
                return Results.Text(reply.Name);
            }).RequestDelegate;

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                var context = new DefaultHttpContext
                {
                    RequestServices = httpHost.Services,
                };
                context.Request.QueryString = new QueryString("?userId=http-user");
                context.Response.Body = new MemoryStream();

                await handler(context);
                context.Response.Body.Position = 0;
                using var reader = new StreamReader(context.Response.Body, leaveOpen: true);
                var content = await reader.ReadToEndAsync();
                if (context.Response.StatusCode != StatusCodes.Status200OK)
                {
                    throw new InvalidOperationException(
                        $"HTTP handler status={context.Response.StatusCode}, body='{content}'.");
                }

                return content;
            },
            static result => string.Equals(result, "user:http-user", StringComparison.Ordinal));

        Assert.Equal("user:http-user", reply);

        await ChannelMessagingTestSupport.StopHostsAsync(httpHost, channelHost);
    }
}
