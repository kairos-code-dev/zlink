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
    public async Task RouteHandler_Uses_IZLinkClient_To_Request_Channel()
    {
        var backendEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var routeLeftEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var routeRightEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftRid = RoutingId.From("41");
        var rightRid = RoutingId.From("42");

        var backendBuilder = Host.CreateApplicationBuilder();
        backendBuilder.Services.AddSingleton<ProfileCommandRecorder>();
        backendBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            {
                var channel = options.AddClientServerChannel("backend");
                channel.EnableServer(backendEndpoint);
                channel.AddHandlerGroup("profile");

            }
        });

        var leftBuilder = Host.CreateApplicationBuilder();
        leftBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var route = options.AddRouteMeshChannel("route");
                route.EnableServer(routeLeftEndpoint);
                route.ConfigureRouting().RoutingId = leftRid;
                route.EnableClient(routeRightEndpoint);

            }
        });

        var rightBuilder = Host.CreateApplicationBuilder();
        rightBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            {
                var route = options.AddRouteMeshChannel("route");
                route.EnableServer(routeRightEndpoint);
                route.ConfigureRouting().RoutingId = rightRid;
                route.EnableClient(routeLeftEndpoint);
                route.AddRequestHandler<RouteForwardProfileHandler, ForwardProfileRequest, ProfileReply>();

            }
            {
                var channel = options.AddClientServerChannel("backend");
                channel.EnableClient(backendEndpoint);

            }
        });

        using var backendHost = backendBuilder.Build();
        using var leftHost = leftBuilder.Build();
        using var rightHost = rightBuilder.Build();

        await ChannelMessagingTestSupport.RunWithHostCleanupAsync(async () =>
        {
            await backendHost.StartAsync();
            await rightHost.StartAsync();
            await leftHost.StartAsync();

            var client = leftHost.Services.GetRequiredService<IZLinkRouteClient>();
            var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client
                    .Request("route", rightRid, new ForwardProfileRequest { UserId = "route-forwarded" })
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<ProfileReply>(),
                static result => result.Name == "user:route-forwarded",
                attempts: 30,
                delayMs: 100);

            Assert.Equal("user:route-forwarded", reply.Name);
        }, leftHost, rightHost, backendHost);
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
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            {
                var channel = options.AddClientServerChannel("backend");
                channel.EnableServer(backendEndpoint);
                channel.AddHandlerGroup("profile");

            }
        });

        var apiBuilder = Host.CreateApplicationBuilder();
        apiBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableServer(apiEndpoint);
                channel.AddHandlerGroup("profile-forward");

            }
            {
                var channel = options.AddClientServerChannel("backend");
                channel.EnableClient(backendEndpoint);

            }
        });

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableClient(apiEndpoint);

            }
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
                .RequestToChannel("api", new ForwardProfileRequest { UserId = "forwarded" })
                .Async<ProfileReply>(),
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
            {
                var channel = options.AddFanoutChannel("profile");
                channel.EnableSubscriber(pubEndpoint);
                channel.AddHandlerGroup("profile-events");

            }
        });

        var apiBuilder = Host.CreateApplicationBuilder();
        apiBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<HandlerClientsTests>();
            {
                var channel = options.AddFanoutChannel("profile");
                channel.EnablePublisher(pubEndpoint);

            }
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableServer(apiEndpoint);
                channel.AddHandlerGroup("profile-publisher");

            }
        });

        var clientBuilder = Host.CreateApplicationBuilder();
        clientBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableClient(apiEndpoint);

            }
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
                    .RequestToChannel("api", new PublishProfileRequest { UserId = "published" })
                    .Async<PublishProfileReply>();
                Assert.True(reply.Accepted);
                await Task.Yield();
                return recorder.Events.Count;
            },
            count => count > 0);

        Assert.Contains("published", recorder.Events.ToArray());

        await ChannelMessagingTestSupport.StopHostsAsync(clientHost, apiHost, subscriberHost);
    }

    public sealed class RouteForwardProfileHandler(IZLinkChannelClient client)
        : IZLinkRouteRequestHandler<ForwardProfileRequest, ProfileReply>
    {
        public async ValueTask<ProfileReply> HandleAsync(
            ForwardProfileRequest request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            return await client
                .RequestToChannel("backend", new GetProfileRequest { UserId = request.UserId })
                .Async<ProfileReply>(cancellationToken);
        }
    }
}
