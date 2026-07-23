using System.Net;
using System.Net.Sockets;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class ClientServerChannelRuntimeTests
{
    [Fact]
    public async Task ManualClient_RequestUsesDedicatedDealerAndServerRouter()
    {
        var port = ReservePort();
        await using var server = CreateServer(port);
        await using var client = CreateClient(port);
        var serverRuntime = server.GetRequiredService<ZLinkFrameworkRuntime>();
        var clientRuntime = client.GetRequiredService<ZLinkFrameworkRuntime>();
        Exception? runtimeFailure = null;
        serverRuntime.ErrorSink.UnhandledCallbackException += exception =>
            runtimeFailure = exception;

        await serverRuntime.StartAsync(CancellationToken.None);
        await clientRuntime.StartAsync(CancellationToken.None);
        try
        {
            var sent = await client.GetRequiredService<IZLinkRouteClient>()
                .SendToChannel("work", new EchoSend("queued"))
                .SubmitAsync();
            Assert.Equal(ZLinkSubmitStatus.Submitted, sent.Status);
            Assert.Equal(
                "queued",
                await server.GetRequiredService<EchoProbe>().Received.Task
                    .WaitAsync(TimeSpan.FromSeconds(5)));

            EchoReply reply;
            try
            {
                reply = await client.GetRequiredService<IZLinkRouteClient>()
                    .RequestToChannel("work", new EchoRequest("ready"))
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<EchoReply>();
            }
            catch when (runtimeFailure is not null)
            {
                throw new InvalidOperationException(
                    "ClientServer server runtime failed.",
                    runtimeFailure);
            }

            Assert.Equal("ready", reply.Value);
        }
        finally
        {
            await clientRuntime.StopAsync(CancellationToken.None);
            await serverRuntime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task AutomaticClient_UsesDedicatedDescriptorAndActualBoundEndpoint()
    {
        var store = new ZLinkInMemoryLocationStore();
        await using var server = CreateAutomaticServer(store, "only");
        await using var client = CreateAutomaticClient(store);
        var serverRuntime = server.GetRequiredService<ZLinkFrameworkRuntime>();
        var clientRuntime = client.GetRequiredService<ZLinkFrameworkRuntime>();
        var serverLocations = server.GetRequiredService<ZLinkLocationRuntime>();
        var clientLocations = client.GetRequiredService<ZLinkLocationRuntime>();
        var serverDiscovery = server.GetRequiredService<ZLinkLocationAutoConnectHost>();
        var clientDiscovery = client.GetRequiredService<ZLinkLocationAutoConnectHost>();

        await serverLocations.StartAsync(RoutingId.From("server-owner"));
        await clientLocations.StartAsync(RoutingId.From("client-owner"));
        await serverRuntime.StartAsync(CancellationToken.None);
        await clientRuntime.StartAsync(CancellationToken.None);
        try
        {
            await serverDiscovery.StartAsync(
                await serverRuntime.EnsureStartedStateAsync(CancellationToken.None));
            await clientDiscovery.StartAsync(
                await clientRuntime.EnsureStartedStateAsync(CancellationToken.None));

            var descriptorPage = await store.ListClientServersAsync(
                "work",
                new ZLinkPageRequest(16));
            var descriptor = Assert.Single(descriptorPage.Items);
            Assert.NotEqual(0UL, descriptor.LifecycleGeneration);
            Assert.NotEqual(0UL, descriptor.DescriptorRevision);
            Assert.NotEqual(0, descriptor.ServerRid.Size);
            Assert.DoesNotContain(":0", descriptor.Endpoint, StringComparison.Ordinal);

            var reply = await client.GetRequiredService<IZLinkRouteClient>()
                .RequestToChannel("work", new EchoRequest("automatic"))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EchoReply>();
            Assert.Equal("only:automatic", reply.Value);

            Assert.True(await serverDiscovery.MarkDrainingAsync());
            descriptorPage = await store.ListClientServersAsync(
                "work",
                new ZLinkPageRequest(16));
            descriptor = Assert.Single(descriptorPage.Items);
            Assert.Equal(ZLinkFrameworkRuntimeState.Draining, descriptor.State);
            Assert.Equal(0, descriptor.Weight);
            Assert.Equal(2UL, descriptor.DescriptorRevision);
        }
        finally
        {
            await clientDiscovery.StopAsync();
            await serverDiscovery.StopAsync();
            await clientRuntime.StopAsync(CancellationToken.None);
            await serverRuntime.StopAsync(CancellationToken.None);
            await clientLocations.StopAsync();
            await serverLocations.StopAsync();
        }

        Assert.Empty((await store.ListClientServersAsync(
            "work",
            new ZLinkPageRequest(16))).Items);
    }

    [Fact]
    public async Task AutomaticClient_SelectsAcrossPositiveWeightReadyServers()
    {
        var store = new ZLinkInMemoryLocationStore();
        await using var first = CreateAutomaticServer(store, "first");
        await using var second = CreateAutomaticServer(store, "second");
        await using var excluded = CreateAutomaticServer(store, "excluded", weight: 0);
        await using var client = CreateAutomaticClient(store);
        var providers = new[] { first, second, excluded, client };
        foreach (var provider in providers)
            await provider.GetRequiredService<ZLinkLocationRuntime>()
                .StartAsync(RoutingId.From(Guid.NewGuid().ToString("N")));
        foreach (var provider in providers)
            await provider.GetRequiredService<ZLinkFrameworkRuntime>()
                .StartAsync(CancellationToken.None);
        try
        {
            foreach (var provider in providers)
                await provider.GetRequiredService<ZLinkLocationAutoConnectHost>()
                    .StartAsync(await provider.GetRequiredService<ZLinkFrameworkRuntime>()
                        .EnsureStartedStateAsync(CancellationToken.None));

            var route = client.GetRequiredService<IZLinkRouteClient>();
            var selected = new HashSet<string>(StringComparer.Ordinal);
            for (var index = 0; index < 12; index++)
            {
                var reply = await route.RequestToChannel(
                        "work",
                        new EchoRequest(index.ToString()))
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<EchoReply>();
                selected.Add(reply.Value.Split(':', 2)[0]);
            }

            Assert.Equal(
                new[] { "first", "second" },
                selected.Order(StringComparer.Ordinal));
        }
        finally
        {
            foreach (var provider in providers.Reverse())
                await provider.GetRequiredService<ZLinkLocationAutoConnectHost>().StopAsync();
            foreach (var provider in providers.Reverse())
                await provider.GetRequiredService<ZLinkFrameworkRuntime>()
                    .StopAsync(CancellationToken.None);
            foreach (var provider in providers.Reverse())
                await provider.GetRequiredService<ZLinkLocationRuntime>().StopAsync();
        }
    }

    [Fact]
    public async Task ServerRestart_ReusesRidAndPublishesNewLifecycleIntent()
    {
        var store = new ZLinkInMemoryLocationStore();
        var port = ReservePort();
        await using var server = CreateAutomaticServer(store, "restart", port: port);
        await using var client = CreateAutomaticClient(store);
        var serverRuntime = server.GetRequiredService<ZLinkFrameworkRuntime>();
        var clientRuntime = client.GetRequiredService<ZLinkFrameworkRuntime>();
        var serverLocations = server.GetRequiredService<ZLinkLocationRuntime>();
        var clientLocations = client.GetRequiredService<ZLinkLocationRuntime>();
        var serverDiscovery = server.GetRequiredService<ZLinkLocationAutoConnectHost>();
        var clientDiscovery = client.GetRequiredService<ZLinkLocationAutoConnectHost>();

        await serverLocations.StartAsync(RoutingId.From("restart-owner"));
        await clientLocations.StartAsync(RoutingId.From("restart-client"));
        await serverRuntime.StartAsync(CancellationToken.None);
        await clientRuntime.StartAsync(CancellationToken.None);
        try
        {
            await serverDiscovery.StartAsync(
                await serverRuntime.EnsureStartedStateAsync(CancellationToken.None));
            await clientDiscovery.StartAsync(
                await clientRuntime.EnsureStartedStateAsync(CancellationToken.None));
            var first = Assert.Single((await store.ListClientServersAsync(
                "work",
                new ZLinkPageRequest(16))).Items);
            var clientBundle = clientRuntime.GetClientServerClientBundle("work");
            Assert.Equal(1, clientBundle.AutoConnectAttemptCount);

            await serverDiscovery.StopAsync();
            await serverRuntime.StopAsync(CancellationToken.None);
            await serverRuntime.StartAsync(CancellationToken.None);
            await serverDiscovery.StartAsync(
                await serverRuntime.EnsureStartedStateAsync(CancellationToken.None));

            var second = Assert.Single((await store.ListClientServersAsync(
                "work",
                new ZLinkPageRequest(16))).Items);
            Assert.Equal(first.ServerRid, second.ServerRid);
            Assert.NotEqual(first.LifecycleGeneration, second.LifecycleGeneration);
            Assert.Equal(first.Endpoint, second.Endpoint);
            Assert.Equal(1UL, second.DescriptorRevision);

            await Task.Delay(TimeSpan.FromSeconds(2));
            Assert.True(clientBundle.AutoDisconnectAttemptCount >= 1);
            Assert.True(clientBundle.AutoConnectAttemptCount >= 2);
            var reply = await client.GetRequiredService<IZLinkRouteClient>()
                .RequestToChannel("work", new EchoRequest("after-restart"))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EchoReply>();
            Assert.Equal("restart:after-restart", reply.Value);
        }
        finally
        {
            await clientDiscovery.StopAsync();
            await serverDiscovery.StopAsync();
            await clientRuntime.StopAsync(CancellationToken.None);
            await serverRuntime.StopAsync(CancellationToken.None);
            await clientLocations.StopAsync();
            await serverLocations.StopAsync();
        }
    }

    [Fact]
    public void ClientServerRegistration_SelectsExactlyOneRole()
    {
        var unselected = Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(
                options => options.AddClientServerChannel("work")));
        Assert.Contains("exactly once", unselected.Message, StringComparison.Ordinal);

        var services = new ServiceCollection();
        Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                var channel = options.AddClientServerChannel("work");
                channel.Client();
                channel.Server();
            }));
    }

    private static ServiceProvider CreateServer(int port)
    {
        var services = new ServiceCollection();
        services.AddSingleton<EchoProbe>();
        services.AddSingleton(new ServerIdentity(string.Empty));
        services.AddZLinkFramework(options =>
            options.AddClientServerChannel("work")
                .Server()
                .Listen(port)
                .AddSendHandler<EchoSendHandler, EchoSend>()
                .AddRequestHandler<EchoHandler, EchoRequest, EchoReply>());
        return services.BuildServiceProvider();
    }

    private static ServiceProvider CreateClient(int port)
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
            options.AddClientServerChannel("work")
                .Client()
                .Connect($"tcp://127.0.0.1:{port}"));
        return services.BuildServiceProvider();
    }

    private static ServiceProvider CreateAutomaticServer(
        ZLinkInMemoryLocationStore store,
        string name,
        int weight = 100,
        int port = 0)
    {
        var services = new ServiceCollection();
        services.AddSingleton<EchoProbe>();
        services.AddSingleton(new ServerIdentity(name));
        services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(store);
            options.AddClientServerChannel("work")
                .Server()
                .Listen(port)
                .SetWeight(weight)
                .AddSendHandler<EchoSendHandler, EchoSend>()
                .AddRequestHandler<EchoHandler, EchoRequest, EchoReply>();
        });
        return services.BuildServiceProvider();
    }

    private static ServiceProvider CreateAutomaticClient(
        ZLinkInMemoryLocationStore store)
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(store);
            options.AddClientServerChannel("work").Client();
        });
        return services.BuildServiceProvider();
    }

    private static int ReservePort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    private sealed record EchoRequest(string Value);

    private sealed record EchoReply(string Value);

    private sealed record EchoSend(string Value);

    private sealed record ServerIdentity(string Name);

    private sealed class EchoProbe
    {
        public TaskCompletionSource<string> Received { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class EchoSendHandler(EchoProbe probe) : IZLinkSendHandler<EchoSend>
    {
        public ValueTask HandleAsync(
            EchoSend message,
            ZLinkSendContext context,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Assert.Equal("work", context.ChannelName);
            probe.Received.TrySetResult(message.Value);
            return ValueTask.CompletedTask;
        }
    }

    private sealed class EchoHandler(ServerIdentity identity)
        : IZLinkRequestHandler<EchoRequest, EchoReply>
    {
        public ValueTask<EchoReply> HandleAsync(
            EchoRequest request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Assert.Equal("work", context.ChannelName);
            return ValueTask.FromResult(new EchoReply(
                string.IsNullOrEmpty(identity.Name)
                    ? request.Value
                    : $"{identity.Name}:{request.Value}"));
        }
    }
}
