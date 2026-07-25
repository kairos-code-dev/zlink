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
            var clientTransport =
                clientRuntime.GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => clientTransport.ReadyCount == 1,
                TimeSpan.FromSeconds(10));
            Assert.True(
                clientTransport.ReadyCount == 1,
                clientTransport.AdmissionDiagnostics);
            var serverState = await serverRuntime.EnsureStartedStateAsync(
                CancellationToken.None);
            var serverIdentity = serverState.ClientServerServerBundles["work"]
                .ClientServerServer!;
            try
            {
                await WaitUntilAsync(
                    () => clientTransport.LivenessAckCount > 0
                        && serverIdentity.LivenessAckCount > 0,
                    TimeSpan.FromSeconds(8));
            }
            catch (TimeoutException exception)
            {
                throw new TimeoutException(
                    $"clientAck={clientTransport.LivenessAckCount}, clientProbe={clientTransport.ReceivedLivenessProbeCount}, clientSent={clientTransport.SentLivenessProbeCount}, serverAck={serverIdentity.LivenessAckCount}, serverProbe={serverIdentity.LivenessProbeCount}, serverReceived={serverIdentity.ReceivedLivenessProbeCount}, peers={serverIdentity.AdmittedPeerCount}, {clientTransport.AdmissionDiagnostics}",
                    exception);
            }
            await client.GetRequiredService<IZLinkRouteClient>()
                .SendToChannel("work", new EchoSend("queued"))
                .Async();
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
    public async Task ServerPushedDrainingUpdate_RemovesManualClientFromReadySet()
    {
        var port = ReservePort();
        await using var server = CreateServer(port);
        await using var client = CreateClient(port);
        var serverRuntime = server.GetRequiredService<ZLinkFrameworkRuntime>();
        var clientRuntime = client.GetRequiredService<ZLinkFrameworkRuntime>();

        await serverRuntime.StartAsync(CancellationToken.None);
        await clientRuntime.StartAsync(CancellationToken.None);
        try
        {
            var transport =
                clientRuntime.GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => transport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));
            var serverState = await serverRuntime.EnsureStartedStateAsync(
                CancellationToken.None);
            serverState.ClientServerServerBundles["work"]
                .ClientServerServer!
                .MarkDraining();
            await WaitUntilAsync(
                () => transport.ReadyCount == 0,
                TimeSpan.FromSeconds(8));
        }
        finally
        {
            await clientRuntime.StopAsync(CancellationToken.None);
            await serverRuntime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task LocalOnlyClientAndServer_UseDealerRouterTransportWithoutLocationStore()
    {
        await using var provider = CreateLocalClientAndServer();
        var runtime = provider.GetRequiredService<ZLinkFrameworkRuntime>();

        await runtime.StartAsync(CancellationToken.None);
        try
        {
            var transport = runtime.GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => transport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));

            Assert.Equal(1, transport.PhysicalConnectionCount);
            var reply = await provider.GetRequiredService<IZLinkRouteClient>()
                .RequestToChannel("work", new EchoRequest("local"))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EchoReply>();
            Assert.Equal("local:local", reply.Value);

            var state = await runtime.EnsureStartedStateAsync(
                CancellationToken.None);
            state.ClientServerServerBundles["work"]
                .ClientServerServer!
                .MarkDraining();
            await WaitUntilAsync(
                () => transport.ReadyCount == 0,
                TimeSpan.FromSeconds(2));
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task LocalOnlyClient_DoesNotSelectZeroWeightServer()
    {
        await using var provider = CreateLocalClientAndServer(weight: 0);
        var runtime = provider.GetRequiredService<ZLinkFrameworkRuntime>();

        await runtime.StartAsync(CancellationToken.None);
        try
        {
            var transport = runtime.GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => transport.AdmissionCompletedCount == 1,
                TimeSpan.FromSeconds(5));
            Assert.Equal(0, transport.ReadyCount);
            var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
                await provider.GetRequiredService<IZLinkRouteClient>()
                    .SendToChannel("work", new EchoSend("excluded"))
                    .Async());
            Assert.Equal(ZLinkFrameworkErrorKind.RequestTargetNotFound, error.Kind);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
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
        await using var first = CreateAutomaticServer(store, "first", weight: 100);
        await using var second = CreateAutomaticServer(store, "second", weight: 300);
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

            var clientTransport =
                client.GetRequiredService<ZLinkFrameworkRuntime>()
                    .GetClientServerClientRuntime("work");
            try
            {
                await WaitUntilAsync(
                    () => clientTransport.ReadyCount == 2
                        && clientTransport.AdmissionCompletedCount == 3,
                    TimeSpan.FromSeconds(10));
            }
            catch (TimeoutException exception)
            {
                throw new TimeoutException(
                    clientTransport.AdmissionDiagnostics,
                    exception);
            }
            var route = client.GetRequiredService<IZLinkRouteClient>();
            var selected = new Dictionary<string, int>(StringComparer.Ordinal);
            for (var index = 0; index < 12; index++)
            {
                var reply = await route.RequestToChannel(
                        "work",
                        new EchoRequest(index.ToString()))
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<EchoReply>();
                var server = reply.Value.Split(':', 2)[0];
                selected[server] = selected.GetValueOrDefault(server) + 1;
            }

            Assert.Equal(
                new[] { "first", "second" },
                selected.Keys.Order(StringComparer.Ordinal));
            Assert.Equal(3, selected["first"]);
            Assert.Equal(9, selected["second"]);

            var runtimeOptions =
                first.GetRequiredService<IZLinkRouteMeshRuntimeOptions>();
            runtimeOptions.Channel("work").Weight = 0;
            Assert.Throws<ZLinkConfigurationException>(
                () => runtimeOptions.Channel("work").Weight = 10_001);
            await WaitUntilAsync(
                () => clientTransport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));
            for (var index = 0; index < 4; index++)
            {
                var reply = await route.RequestToChannel(
                        "work",
                        new EchoRequest($"after-{index}"))
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<EchoReply>();
                Assert.StartsWith("second:", reply.Value);
            }
            var ownerId = first.GetRequiredService<ZLinkLocationRuntime>()
                .OwnerId;
            ZLinkClientServerServerDescriptor? updated = null;
            var updateDeadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
            while (DateTime.UtcNow < updateDeadline)
            {
                updated = (await store.ListClientServersAsync(
                        "work",
                        new ZLinkPageRequest(16))).Items
                    .Single(row => row.OwnerId == ownerId);
                if (updated.Weight == 0)
                    break;
                await Task.Delay(10);
            }
            Assert.Equal(0, updated?.Weight);
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
    public async Task ManualAndAutomaticIntents_ShareAdmittedServerIdentity()
    {
        var store = new ZLinkInMemoryLocationStore();
        await using var server = CreateAutomaticServer(store, "shared");
        await using var client = CreateAutomaticClient(store);
        var serverRuntime = server.GetRequiredService<ZLinkFrameworkRuntime>();
        var clientRuntime = client.GetRequiredService<ZLinkFrameworkRuntime>();
        var serverLocations = server.GetRequiredService<ZLinkLocationRuntime>();
        var clientLocations = client.GetRequiredService<ZLinkLocationRuntime>();
        var serverDiscovery = server.GetRequiredService<ZLinkLocationAutoConnectHost>();
        var clientDiscovery = client.GetRequiredService<ZLinkLocationAutoConnectHost>();

        await serverLocations.StartAsync(RoutingId.From("shared-server"));
        await clientLocations.StartAsync(RoutingId.From("shared-client"));
        await serverRuntime.StartAsync(CancellationToken.None);
        await clientRuntime.StartAsync(CancellationToken.None);
        try
        {
            await serverDiscovery.StartAsync(
                await serverRuntime.EnsureStartedStateAsync(CancellationToken.None));
            await clientDiscovery.StartAsync(
                await clientRuntime.EnsureStartedStateAsync(CancellationToken.None));

            var descriptor = Assert.Single((await store.ListClientServersAsync(
                "work",
                new ZLinkPageRequest(16))).Items);
            var transport =
                clientRuntime.GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => transport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));

            transport.AddManual(descriptor.Endpoint);
            await WaitUntilAsync(
                () => transport.ConnectionIntentCount == 2
                    && transport.PhysicalConnectionCount == 1,
                TimeSpan.FromSeconds(5));

            transport.RemoveManual(descriptor.Endpoint);
            Assert.Equal(1, transport.ConnectionIntentCount);
            Assert.Equal(1, transport.PhysicalConnectionCount);
            var reply = await client.GetRequiredService<IZLinkRouteClient>()
                .RequestToChannel("work", new EchoRequest("deduped"))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EchoReply>();
            Assert.Equal("shared:deduped", reply.Value);
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
            var clientTransport =
                clientRuntime.GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => clientTransport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));

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
            await WaitUntilAsync(
                () => clientTransport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));
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

    private static async Task WaitUntilAsync(
        Func<bool> condition,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (!condition())
        {
            if (DateTime.UtcNow >= deadline)
                throw new TimeoutException("ClientServer condition was not reached.");
            await Task.Delay(10);
        }
    }

    [Fact]
    public void ClientServerRegistration_AllowsEachRoleOnceAndRejectsDuplicateRole()
    {
        var unselected = Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(
                options => options.AddClientServerChannel("work")));
        Assert.Contains("at least once", unselected.Message, StringComparison.Ordinal);

        var services = new ServiceCollection();
        services.AddSingleton<EchoProbe>();
        services.AddSingleton(new ServerIdentity("local"));
        services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("work").Client().Connect("tcp://127.0.0.1:7001");
            options.AddClientServerChannel("work")
                .Server()
                .Listen(0)
                .AddRequestHandler<EchoHandler, EchoRequest, EchoReply>();
        });

        var duplicateClient = Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(options =>
            {
                options.AddClientServerChannel("work").Client().Connect("tcp://127.0.0.1:7001");
                options.AddClientServerChannel("work").Client().Connect("tcp://127.0.0.1:7002");
            }));
        Assert.Contains("role 'Client' is already registered", duplicateClient.Message, StringComparison.Ordinal);

        var duplicateServer = Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(options =>
            {
                options.AddClientServerChannel("work")
                    .Server()
                    .Listen(0)
                    .AddRequestHandler<EchoHandler, EchoRequest, EchoReply>();
                options.AddClientServerChannel("work").Server();
            }));
        Assert.Contains("role 'Server' is already registered", duplicateServer.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void ClientServerRegistration_AllowsDifferentNamesAndRejectsRouteMeshCollision()
    {
        var services = new ServiceCollection();
        services.AddSingleton<EchoProbe>();
        services.AddSingleton(new ServerIdentity("local"));
        services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("orders").Client().Connect("tcp://127.0.0.1:7001");
            options.AddClientServerChannel("billing").Client().Connect("tcp://127.0.0.1:7002");
            options.AddClientServerChannel("orders-server")
                .Server()
                .Listen(0)
                .AddRequestHandler<EchoHandler, EchoRequest, EchoReply>();
            options.AddClientServerChannel("billing-server")
                .Server()
                .Listen(0)
                .AddRequestHandler<EchoHandler, EchoRequest, EchoReply>();
        });

        var collision = Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(options =>
            {
                options.AddRouteMesh("mesh")
                    .Listen("tcp://127.0.0.1:0")
                    .ChannelName("orders");
                options.AddClientServerChannel("orders")
                    .Client()
                    .Connect("tcp://127.0.0.1:7001");
            }));
        Assert.Contains(
            "registered on both RouteMesh and ClientServer physical paths",
            collision.Message,
            StringComparison.Ordinal);
    }

    [Fact]
    public async Task AutomaticClient_IncludesSameProcessServerWithoutLocalPreferenceOrBypass()
    {
        var store = new ZLinkInMemoryLocationStore();
        await using var local = CreateAutomaticClientAndServer(store, "local");
        await using var remote = CreateAutomaticServer(store, "remote");
        var providers = new[] { local, remote };

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

            var transport = local.GetRequiredService<ZLinkFrameworkRuntime>()
                .GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => transport.ReadyCount == 2,
                TimeSpan.FromSeconds(10));

            var route = local.GetRequiredService<IZLinkRouteClient>();
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
                new[] { "local", "remote" },
                selected.OrderBy(static value => value, StringComparer.Ordinal));

            Assert.True(
                await local.GetRequiredService<ZLinkLocationAutoConnectHost>()
                    .MarkDrainingAsync());
            await WaitUntilAsync(
                () => transport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));
            var afterDrain = await route.RequestToChannel(
                    "work",
                    new EchoRequest("after-drain"))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EchoReply>();
            Assert.Equal("remote:after-drain", afterDrain.Value);
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
    public async Task AutomaticClient_ExcludesSameProcessZeroWeightServer()
    {
        var store = new ZLinkInMemoryLocationStore();
        await using var local = CreateAutomaticClientAndServer(
            store,
            "local",
            weight: 0);
        await using var remote = CreateAutomaticServer(store, "remote");
        var providers = new[] { local, remote };

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

            var transport = local.GetRequiredService<ZLinkFrameworkRuntime>()
                .GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => transport.ReadyCount == 1
                    && transport.AdmissionCompletedCount == 2,
                TimeSpan.FromSeconds(10));

            var reply = await local.GetRequiredService<IZLinkRouteClient>()
                .RequestToChannel("work", new EchoRequest("weighted"))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EchoReply>();
            Assert.Equal("remote:weighted", reply.Value);
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
    public void ClientServerControlProtocol_UsesExactBinaryAdmissionRecords()
    {
        using var hello = ZLinkClientServerControlProtocol.EncodeHello(
            new ZLinkClientServerControlProtocol.Hello(
                "work",
                "plaintext",
                4096));
        Assert.Equal(
            "5A4D010100020000001701001404776F726B0109706C61696E7465787400001000",
            Convert.ToHexString(hello.AsReadOnlyMemory().Span));
        Assert.True(
            ZLinkClientServerControlProtocol.TryDecodeHello(
                [hello],
                out var decodedHello));
        Assert.Equal("work", decodedHello!.ChannelName);
        Assert.Equal("plaintext", decodedHello.SecurityIdentity);
        Assert.Equal(4096U, decodedHello.NormalizedEffectiveMaxMessageBytes);

        using var admit = ZLinkClientServerControlProtocol.EncodeAdmission(
            new ZLinkClientServerControlProtocol.Admission(
                "work",
                RoutingId.From("server-a"),
                3,
                7,
                75,
                ZLinkFrameworkRuntimeState.Serving,
                "plaintext",
                4096,
                "tcp://127.0.0.1:7002"));
        Assert.True(
            ZLinkClientServerControlProtocol.TryDecodeAdmission(
                [admit],
                out var decodedAdmission));
        Assert.Equal(RoutingId.From("server-a"), decodedAdmission!.ServerRid);
        Assert.Equal(3UL, decodedAdmission.LifecycleGeneration);
        Assert.Equal(7UL, decodedAdmission.DescriptorRevision);
        Assert.Equal(75, decodedAdmission.Weight);
        Assert.Equal("tcp://127.0.0.1:7002", decodedAdmission.AdvertisedEndpoint);

        using var update = ZLinkClientServerControlProtocol.EncodeUpdate(
            decodedAdmission with
            {
                DescriptorRevision = 8,
                Weight = 0,
                State = ZLinkFrameworkRuntimeState.Draining
            });
        Assert.True(
            ZLinkClientServerControlProtocol.TryDecodeUpdate(
                [update],
                out var decodedUpdate));
        Assert.Equal(8UL, decodedUpdate!.DescriptorRevision);
        Assert.Equal(0, decodedUpdate.Weight);
        Assert.Equal(
            ZLinkFrameworkRuntimeState.Draining,
            decodedUpdate.State);

        using var probe =
            ZLinkClientServerControlProtocol.EncodeLivenessProbe(11);
        Assert.Equal(
            "5A4D010500000000000000000B",
            Convert.ToHexString(probe.AsReadOnlyMemory().Span));
        Assert.True(
            ZLinkClientServerControlProtocol.TryDecodeLivenessProbe(
                [probe],
                out var probeId));
        Assert.Equal(11UL, probeId);

        using var ack =
            ZLinkClientServerControlProtocol.EncodeLivenessAck(11);
        Assert.Equal(
            "5A4D010600000000000000000B",
            Convert.ToHexString(ack.AsReadOnlyMemory().Span));
        Assert.True(
            ZLinkClientServerControlProtocol.TryDecodeLivenessAck(
                [ack],
                out var ackId));
        Assert.Equal(11UL, ackId);

        using var reject = ZLinkClientServerControlProtocol.EncodeReject(1);
        Assert.Equal(
            "5A4D01030000000001",
            Convert.ToHexString(reject.AsReadOnlyMemory().Span));
        Assert.True(
            ZLinkClientServerControlProtocol.TryDecodeReject(
                [reject],
                out var reason));
        Assert.Equal(1U, reason);
    }

    [Fact]
    public async Task BackendWrappers_DeliverUnsolicitedLivenessProbe()
    {
        using var context = Systems.Zlink.Zlink.CreateContext();
        await using var router = new ZLinkBackendRouterSocketWrapper(
            context.CreateRouterSocket());
        await using var dealer = new ZLinkBackendDealerSocketWrapper(
            context.CreateDealerSocket());
        var port = ReservePort();
        var endpoint = $"tcp://127.0.0.1:{port}";
        dealer.SetRoutingId(RoutingId.From("probe-client"));
        router.Bind(endpoint);
        dealer.Connect(endpoint);

        var hello = ZLinkClientServerControlProtocol.EncodeHello(
            new ZLinkClientServerControlProtocol.Hello(
                "work",
                "plaintext",
                4096));
        var admissionTask = dealer.RequestAsync(
            hello,
            TimeSpan.FromSeconds(2),
            CancellationToken.None);
        using var inbound = await PollReceivedAsync(
            () => router.Recv(RecvFlags.DontWait),
            TimeSpan.FromSeconds(2));
        var sourceRid = Assert.IsType<RoutingId>(inbound.RoutingId);
        var requestSeq = Assert.IsType<ulong>(inbound.RequestSeq);
        var admit = ZLinkClientServerControlProtocol.EncodeAdmission(
            new ZLinkClientServerControlProtocol.Admission(
                "work",
                RoutingId.From("probe-server"),
                1,
                1,
                100,
                ZLinkFrameworkRuntimeState.Serving,
                "plaintext",
                4096,
                endpoint));
        router.Reply(sourceRid, requestSeq, admit);
        ZLinkMessageParts.DisposeAll(await admissionTask);

        var probe =
            ZLinkClientServerControlProtocol.EncodeLivenessProbe(17);
        Assert.True(router.Send(sourceRid, probe, SendFlags.None));
        using var delivered = await PollReceivedAsync(
            () => dealer.Recv(RecvFlags.DontWait),
            TimeSpan.FromSeconds(2));
        Assert.True(
            ZLinkClientServerControlProtocol.TryDecodeLivenessProbe(
                delivered.Parts,
                out var probeId));
        Assert.Equal(17UL, probeId);
    }

    private static async Task<Received> PollReceivedAsync(
        Func<Received?> receive,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (receive() is { } received)
                return received;
            await Task.Delay(5);
        }
        throw new TimeoutException("Expected raw record was not received.");
    }

    [Fact]
    public async Task AutomaticClient_RejectsDescriptorWhoseAdmissionIdentityDoesNotMatch()
    {
        var store = new ZLinkInMemoryLocationStore();
        await using var server = CreateAutomaticServer(store, "identity");
        await using var client = CreateAutomaticClient(store);
        var serverLocations = server.GetRequiredService<ZLinkLocationRuntime>();
        var clientLocations = client.GetRequiredService<ZLinkLocationRuntime>();
        var serverRuntime = server.GetRequiredService<ZLinkFrameworkRuntime>();
        var clientRuntime = client.GetRequiredService<ZLinkFrameworkRuntime>();
        var serverDiscovery =
            server.GetRequiredService<ZLinkLocationAutoConnectHost>();
        var clientDiscovery =
            client.GetRequiredService<ZLinkLocationAutoConnectHost>();

        await serverLocations.StartAsync(RoutingId.From("identity-owner"));
        await clientLocations.StartAsync(RoutingId.From("identity-client"));
        await serverRuntime.StartAsync(CancellationToken.None);
        await clientRuntime.StartAsync(CancellationToken.None);
        try
        {
            await serverDiscovery.StartAsync(
                await serverRuntime.EnsureStartedStateAsync(
                    CancellationToken.None));
            var actual = Assert.Single(
                (await store.ListClientServersAsync(
                    "work",
                    new ZLinkPageRequest(16))).Items);
            Assert.Equal(
                ZLinkLocationWriteStatus.Stored,
                await store.RemoveClientServerAsync(
                    new ZLinkClientServerServerDescriptorKey(
                        actual.ChannelName,
                        actual.ServerRid),
                    serverLocations.OwnerToken));
            var forged = actual with
            {
                LifecycleGeneration = actual.LifecycleGeneration + 1,
                DescriptorRevision = 1,
                SecurityIdentity = "forged-security"
            };
            Assert.Equal(
                ZLinkLocationWriteStatus.Stored,
                (await store.UpdateClientServerAsync(
                    forged,
                    ZLinkLocationWriteIntent.NewClaim)).Status);

            await clientDiscovery.StartAsync(
                await clientRuntime.EnsureStartedStateAsync(
                    CancellationToken.None));
            var transport =
                clientRuntime.GetClientServerClientRuntime("work");
            await WaitUntilAsync(
                () => transport.AdmissionCompletedCount == 1,
                TimeSpan.FromSeconds(5));
            Assert.Equal(0, transport.ReadyCount);
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
    public async Task MalformedPushedControl_ReconnectsAndReadmits()
    {
        var port = ReservePort();
        var endpoint = $"tcp://127.0.0.1:{port}";
        using var context = Systems.Zlink.Zlink.CreateContext();
        using var router = context.CreateRouterSocket();
        router.Bind(endpoint);
        await using var client = CreateClient(port);
        var runtime = client.GetRequiredService<ZLinkFrameworkRuntime>();
        await runtime.StartAsync(CancellationToken.None);
        try
        {
            var transport = runtime.GetClientServerClientRuntime("work");
            using var firstHello = await PollReceivedAsync(
                () => TryReceive(router),
                TimeSpan.FromSeconds(5));
            Assert.True(
                ZLinkClientServerControlProtocol.TryDecodeHello(
                    firstHello.Parts,
                    out _));
            ReplyAdmission(router, firstHello, endpoint);
            await WaitUntilAsync(
                () => transport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));

            var malformed = Message.From(
                new byte[] { 0x5a, 0x4d, 0x01, 0xff, 0x00 });
            Assert.True(
                router.Send(
                    firstHello.RoutingId
                        ?? throw new InvalidOperationException(
                            "missing client routing id"))
                    .Message(malformed)
                    .Flags(SendFlags.DontWait)
                    .Submit());

            using var secondHello = await PollReceivedAsync(
                () => TryReceive(router),
                TimeSpan.FromSeconds(5));
            Assert.True(
                ZLinkClientServerControlProtocol.TryDecodeHello(
                    secondHello.Parts,
                    out _));
            ReplyAdmission(router, secondHello, endpoint);
            await WaitUntilAsync(
                () => transport.ReadyCount == 1,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    private static Received? TryReceive(IRouterSocket router)
    {
        var received = Received.Create();
        if (router.Recv(received, RecvFlags.DontWait))
            return received;
        received.Dispose();
        return null;
    }

    private static void ReplyAdmission(
        IRouterSocket router,
        Received hello,
        string endpoint)
    {
        var admission = ZLinkClientServerControlProtocol.EncodeAdmission(
            new ZLinkClientServerControlProtocol.Admission(
                "work",
                RoutingId.From("manual-server"),
                1,
                1,
                100,
                ZLinkFrameworkRuntimeState.Serving,
                "plaintext",
                1024 * 1024,
                endpoint));
        router.Reply(
                hello.RoutingId
                ?? throw new InvalidOperationException(
                    "missing client routing id"),
                hello.RequestSeq
                ?? throw new InvalidOperationException(
                    "missing request sequence"))
            .Message(admission)
            .Submit();
    }

    [Fact]
    public async Task MalformedReservedControlFrame_DoesNotReachApplicationHandler()
    {
        var port = ReservePort();
        await using var server = CreateServer(port);
        var runtime = server.GetRequiredService<ZLinkFrameworkRuntime>();
        await runtime.StartAsync(CancellationToken.None);
        try
        {
            using var context = Systems.Zlink.Zlink.CreateContext();
            using var dealer = context.CreateDealerSocket();
            dealer.SetRoutingId(RoutingId.From("malformed-client"));
            dealer.Connect($"tcp://127.0.0.1:{port}");
            await Task.Delay(100);
            var malformed = Message.From(
                new byte[] { 0x5a, 0x4d, 0x01, 0xff, 0x00 });
            Assert.True(dealer.Send().Message(malformed).Submit());
            await Task.Delay(250);
            Assert.False(
                server.GetRequiredService<EchoProbe>().Received.Task.IsCompleted);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
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

    private static ServiceProvider CreateAutomaticClientAndServer(
        ZLinkInMemoryLocationStore store,
        string name,
        int weight = 100)
    {
        var services = new ServiceCollection();
        services.AddSingleton<EchoProbe>();
        services.AddSingleton(new ServerIdentity(name));
        services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(store);
            options.AddClientServerChannel("work").Client();
            options.AddClientServerChannel("work")
                .Server()
                .Listen(0)
                .SetWeight(weight)
                .AddSendHandler<EchoSendHandler, EchoSend>()
                .AddRequestHandler<EchoHandler, EchoRequest, EchoReply>();
        });
        return services.BuildServiceProvider();
    }

    private static ServiceProvider CreateLocalClientAndServer(int weight = 100)
    {
        var services = new ServiceCollection();
        services.AddSingleton<EchoProbe>();
        services.AddSingleton(new ServerIdentity("local"));
        services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("work").Client();
            options.AddClientServerChannel("work")
                .Server()
                .Listen(0)
                .SetWeight(weight)
                .AddSendHandler<EchoSendHandler, EchoSend>()
                .AddRequestHandler<EchoHandler, EchoRequest, EchoReply>();
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
            IZLinkMessageContext context,
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
            IZLinkMessageContext context,
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
