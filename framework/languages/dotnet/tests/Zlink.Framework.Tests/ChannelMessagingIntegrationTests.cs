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
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.MapHandlerGroup("profile");
            });
        });
        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.AddClientServerChannel("api", channel =>
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
            async () => await client.Request("api", new GetProfileRequest { UserId = "discovery" }).SubmitAsync<ProfileReply>(),
            static result => result.Name == "user:discovery");

        Assert.Equal("user:discovery", reply.Name);

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                await client.Send("api", new RefreshProfileCacheCommand { UserId = "discovery" }).Submit();
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
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.MapHandlerGroup("profile");
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

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var recorder = serverHost.Services.GetRequiredService<ProfileCommandRecorder>();

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.Request("api", new GetProfileRequest { UserId = "alice" }).SubmitAsync<ProfileReply>(),
            static result => result.Name == "user:alice");

        Assert.Equal("user:alice", reply.Name);

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                await client.Send("api", new RefreshProfileCacheCommand { UserId = "alice" }).Submit();
                await Task.Yield();
                return recorder.Commands.Count;
            },
            count => count > 0);

        Assert.Contains("alice", recorder.Commands.ToArray());

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
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.AddFanoutChannel("profile", channel =>
            {
                channel.EnableSubscriber(subscriber =>
                {
                    subscriber.UseManualConnections(connections => connections.Connect(pubEndpoint));
                });
                channel.MapHandlerGroup("profile-events");
            });
        });
        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddFanoutChannel("profile", channel =>
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
                await publisher.Publish(
                    "profile",
                    "profile.cache-invalidated",
                    new ProfileInvalidated { UserId = "alice" }).Submit();
                await Task.Yield();
                return recorder.Events.Count;
            },
            count => count > 0);

        Assert.Contains("alice", recorder.Events.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(publisherHost, subscriberHost);
    }

    [Fact]
    public async Task ChannelHandler_Uses_IZLinkClient_To_Request_Another_Channel()
    {
        var backendEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var backendBuilder = Host.CreateApplicationBuilder();
        backendBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        backendBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.AddClientServerChannel("backend", channel =>
            {
                channel.EnableServer(server => server.Bind(backendEndpoint));
                channel.MapHandlerGroup("profile");
            });
        });

        var apiBuilder = Host.CreateApplicationBuilder();
        apiBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.MapHandlerGroup("profile-forward");
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

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client
                .Request("api", new ForwardProfileRequest { UserId = "forwarded" })
                .SubmitAsync<ProfileReply>(),
            static result => result.Name == "user:forwarded");

        Assert.Equal("user:forwarded", reply.Name);

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, apiHost, backendHost);
    }

    [Fact]
    public async Task ChannelHandler_Uses_IZLinkFanoutPublisher_To_Publish_Event()
    {
        var pubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<ProfileEventRecorder>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.AddFanoutChannel("profile", channel =>
            {
                channel.EnableSubscriber(subscriber =>
                {
                    subscriber.UseManualConnections(connections => connections.Connect(pubEndpoint));
                });
                channel.MapHandlerGroup("profile-events");
            });
        });

        var apiBuilder = Host.CreateApplicationBuilder();
        apiBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.AddFanoutChannel("profile", channel =>
            {
                channel.EnablePublisher(publisher => publisher.Bind(pubEndpoint));
            });
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.MapHandlerGroup("profile-publisher");
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

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var recorder = subscriberHost.Services.GetRequiredService<ProfileEventRecorder>();
        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                var reply = await client
                    .Request("api", new PublishProfileRequest { UserId = "published" })
                    .SubmitAsync<PublishProfileReply>();
                Assert.True(reply.Accepted);
                await Task.Yield();
                return recorder.Events.Count;
            },
            count => count > 0);

        Assert.Contains("published", recorder.Events.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, apiHost, subscriberHost);
    }

    [Fact]
    public async Task DealerMeshClient_Request_And_Send_Use_Registered_DealerMesh_Channel()
    {
        var meshEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        await using var context = new Context();
        await using var router = new RouterSocket(context);
        router.Bind(meshEndpoint);

        using var stopRouter = new CancellationTokenSource();
        var recorder = new MeshProfileRecorder();
        var routerTask = Task.Run(
            () => RunMeshRouterAsync(router, recorder, stopRouter.Token),
            stopRouter.Token);

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddDealerMeshChannel("mesh", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(meshEndpoint));
                });
            });
        });

        using var clientHost = clientBuilder.Build();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client
                .Request("mesh", new MeshProfileRequest { UserId = "mesh-request" })
                .Timeout(TimeSpan.FromMilliseconds(500))
                .SubmitAsync<MeshProfileReply>(),
            static result => result.Name == "mesh:mesh-request");

        Assert.Equal("mesh:mesh-request", reply.Name);

        await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () =>
            {
                await client
                    .Send("mesh", new MeshProfileRequest { UserId = "mesh-send" })
                    .Submit();
                await Task.Yield();
                return recorder.Commands.Count;
            },
            count => count > 0);

        Assert.Contains("mesh-request", recorder.Requests.ToArray());
        Assert.Contains("mesh-send", recorder.Commands.ToArray());

        await clientHost.StopAsync();
        stopRouter.Cancel();
        await routerTask.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task Filters_Run_In_Registration_Order_Around_Handler_Dispatch()
    {
        var apiEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var serverBuilder = Host.CreateApplicationBuilder();
        serverBuilder.Services.AddSingleton<FilterOrderRecorder>();
        serverBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.UseFilter<OuterOrderFilter>();
            options.UseFilter<InnerOrderFilter>();
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.MapHandlerGroup("filter-order");
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

        using var serverHost = serverBuilder.Build();
        using var clientHost = clientBuilder.Build();

        await serverHost.StartAsync();
        await clientHost.StartAsync();

        var client = clientHost.Services.GetRequiredService<IZLinkClient>();
        var recorder = serverHost.Services.GetRequiredService<FilterOrderRecorder>();

        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.Request("api", new GetFilterOrderRequest()).SubmitAsync<FilterOrderReply>(),
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
            options.AddHandlersFromAssemblyOf<ChannelMessagingIntegrationTests>();
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.MapHandlerGroup("profile");
            });
        });
        var httpBuilder = Host.CreateApplicationBuilder();
        httpBuilder.Services.AddLogging();
        httpBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("api", channel =>
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
                    .SubmitAsync<ProfileReply>(cancellationToken);
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

    private static async Task RunMeshRouterAsync(
        RouterSocket router,
        MeshProfileRecorder recorder,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            using var received = new Received();
            if (!router.Recv(received, RecvFlags.DontWait))
            {
                await Task.Delay(10).ConfigureAwait(false);
                continue;
            }

            var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
            var request = (MeshProfileRequest?)ZLinkEnvelopeCodec.DecodeBody(
                received.Parts,
                typeof(MeshProfileRequest));
            if (request is null)
            {
                continue;
            }

            if (header.Kind == ZLinkMessageKind.Command)
            {
                recorder.Commands.Enqueue(request.UserId);
                continue;
            }

            recorder.Requests.Enqueue(request.UserId);
            var reply = ZLinkEnvelopeCodec.EncodeParts(
                new ZLinkEnvelopeHeader(
                    ZLinkMessageKind.Response,
                    header.ChannelName,
                    header.MessageName,
                    ZLinkEnvelopeCodec.DefaultContentType,
                    header.CorrelationId,
                    null,
                    null,
                    null,
                    null),
                new MeshProfileReply { Name = $"mesh:{request.UserId}" },
                typeof(MeshProfileReply));
            received.Reply().Messages(reply).Submit();
        }
    }
}
