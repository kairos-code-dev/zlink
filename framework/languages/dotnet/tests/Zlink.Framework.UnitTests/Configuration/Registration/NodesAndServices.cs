using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Reflection;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Handlers;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.UnitTests;

public sealed class NodesAndServicesTests : RegistrationValidationSupport
{
    [Fact]
    public void AddZLinkFramework_Rejects_Duplicate_Registration()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(_ => { });

        var error = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(_ => { }));

        Assert.Contains("already configured", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Rejects_TheSameEntrySpotType_AcrossNodes()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddRouteMesh("entry-a")
                    .Listen("inproc://entry-a")
                    .AddEntrySpot<TestEntrySpot>().ChannelName("entry-a");
                options.AddRouteMesh("entry-b")
                    .Listen("inproc://entry-b")
                    .AddEntrySpot<TestEntrySpot>().ChannelName("entry-b");
            }));

        Assert.Contains("Duplicate Entry Spot", exception.Message, StringComparison.Ordinal);
        Assert.Contains(typeof(TestEntrySpot).ToString(), exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                {
                    var stream = options.AddStreamNode("client.stream");
                    stream.Bind("tcp://127.0.0.1:9100");
                    stream.AddSession<TestHeaderSession>();
                    stream.AddSession<TestHeaderSession>();
                }
            }));

        Assert.Contains("already has a stream session", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsStandaloneLocalSpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var mesh = options.AddRouteMesh("stage-node");
                mesh.ChannelName("stage-node");
                {
                    var spot = mesh;
                    {
                        var router = spot.Listen("tcp://127.0.0.1:9000");
                    }
                    spot.AddSpotFactory<TestSpot>();
                }
            }
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Single(registration.SpotNodes);
    }

    [Fact]
    public void AddZLinkFramework_AddRouteMesh_CanConfigureDefaultSpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMesh("stage-node")
                .Listen("tcp://127.0.0.1:9000")
                .AddSpotFactory<TestSpot>().ChannelName("stage-node");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        var node = Assert.Single(registration.SpotNodes.Values);
        Assert.Equal("stage-node", node.SpotNodeName);
        Assert.NotNull(node.Router);
        Assert.Contains(typeof(TestSpot), node.SpotFactories);
    }

    [Fact]
    public void AddZLinkFramework_AddRouteMesh_AllowsMultipleProcessLocalNodes()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMesh("play-node")
                .Listen("tcp://127.0.0.1:9001")
                .AddSpotFactory<TestSpot>().ChannelName("play-node");
            options.AddRouteMesh("session-node")
                .Listen("tcp://127.0.0.1:9002")
                .AddSpotFactory<OtherTestSpot>().ChannelName("session-node");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Equal(new[] { "play-node", "session-node" }, registration.SpotNodes.Keys);
        Assert.Equal("play-node", registration.SpotNodes["play-node"].SpotMeshChannelName);
        Assert.Equal("session-node", registration.SpotNodes["session-node"].SpotMeshChannelName);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotMeshNameIsDuplicated()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddRouteMesh("play-node").Listen("tcp://127.0.0.1:9001").ChannelName("play-node");
                options.AddRouteMesh("play-node").Listen("tcp://127.0.0.1:9002").ChannelName("play-node");
            }));

        Assert.Contains("Duplicate RouteMesh name 'play-node'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotFactoryTypeIsDuplicatedAcrossNodes()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                {
                    var mesh = options.AddRouteMesh("game.stage");
                    mesh.ChannelName("game.stage");
                    {
                        var spot = mesh;
                        {
                            var router = spot.Listen("tcp://127.0.0.1:6101");
                        }
                        spot.AddSpotFactory<TestSpot>();
                    }
                    {
                        var spot = mesh;
                        {
                            var router = spot.Listen("tcp://127.0.0.1:6102");
                        }
                        spot.AddSpotFactory<TestSpot>();
                    }
                }
            }));

        Assert.Contains("Duplicate SPOT factory", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotNodeRegistersMultipleEntrySpots()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                {
                    var mesh = options.AddRouteMesh("game.stage");
                    mesh.ChannelName("game.stage");
                    {
                        var spot = mesh;
                        {
                            var router = spot.Listen("tcp://127.0.0.1:6101");
                        }
                        spot.AddEntrySpot<TestEntrySpot>();
                        spot.AddEntrySpot<TestEntrySpot>();
                    }
                }
            }));

        Assert.Contains("Duplicate Entry Spot registry", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenActorFactoryNameIsDuplicated()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddRouteMesh("actor-node")
                    .Listen("tcp://127.0.0.1:6102")
                    .AddActorFactory<TestActorFactory>("warrior")
                    .AddActorFactory<TestActorFactory>("warrior").ChannelName("actor-node");
            }));

        Assert.Contains("Duplicate actor factory 'warrior'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenActorFactoryNodeHasNoRouterCapability()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddRouteMesh("actor-node")
                    .AddActorFactory<TestActorFactory>("warrior");
            }));

        Assert.Contains("ROUTER endpoint", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_When_Multiple_SpotNodes_Own_ActorFactories()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddRouteMesh("actor-node-a")
                    .Listen("tcp://127.0.0.1:6103")
                    .AddActorFactory<TestActorFactory>("warrior").ChannelName("actor-node-a");
                options.AddRouteMesh("actor-node-b")
                    .Listen("tcp://127.0.0.1:6104")
                    .AddActorFactory<TestActorFactory>("mage").ChannelName("actor-node-b");
            }));

        Assert.Contains("more than one SpotNode owns actor factories", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_ActorManager_Without_SpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(_ => { });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_SpotServices_Without_SpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(_ => { });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkSpotManager>());
        Assert.Null(provider.GetService<IZLinkSpotOutbound>());
        Assert.Null(provider.GetService<IZLinkSpotPublisherClient>());
    }

    [Fact]
    public async Task AddZLinkFramework_Registers_SpotServices_When_SpotNode_Exists()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var mesh = options.AddRouteMesh("stage-node");
                mesh.ChannelName("stage-node");
                {
                    var spot = mesh;
                    {
                        var router = spot.Listen("tcp://127.0.0.1:6200");
                    }
                }
            }
        });

        await using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkSpotManager>());
        Assert.NotNull(provider.GetService<IZLinkSpotOutbound>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_ActorManager_With_SpotNode_Only()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var mesh = options.AddRouteMesh("stage-node");
                mesh.ChannelName("stage-node");
                {
                    var spot = mesh;
                    {
                        var router = spot.Listen("tcp://127.0.0.1:6203");
                    }
                }
            }
        });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public async Task AddZLinkFramework_Registers_ActorManager_When_SpotNode_And_ActorFactory_Exist()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.ActorTransferTimeout = TimeSpan.FromSeconds(15);
            options.ActorTransferForwardWindow = TimeSpan.FromSeconds(5);
            options.AddRouteMesh("actor-node")
                .Listen("tcp://127.0.0.1:6201")
                .AddActorFactory<TestActorFactory>("warrior").ChannelName("actor-node");
        });

        await using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public void AddActorTransferAdapter_Registers_Adapter_As_Scoped_Service()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.ActorTransferTimeout = TimeSpan.FromSeconds(15);
            options.ActorTransferForwardWindow = TimeSpan.FromSeconds(5);
            options.AddRouteMesh("actor-node")
                .Listen("tcp://127.0.0.1:6202")
                .AddActorFactory<TestActorFactory>("warrior")
                .AddActorTransferAdapter<TestActor, TestActorTransferAdapter>("warrior").ChannelName("actor-node");
        });

        using var provider = services.BuildServiceProvider();
        using var scope = provider.CreateScope();
        Assert.NotNull(scope.ServiceProvider.GetService<TestActorTransferAdapter>());
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenActorTransferTypeIsDuplicatedAcrossNodes()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.ActorTransferTimeout = TimeSpan.FromSeconds(15);
                options.ActorTransferForwardWindow = TimeSpan.FromSeconds(5);
                options.AddRouteMesh("actor-node-a")
                    .Listen("tcp://127.0.0.1:6201")
                    .AddActorTransferAdapter<TestActor, TestActorTransferAdapter>("warrior").ChannelName("actor-node-a");
                options.AddRouteMesh("actor-node-b")
                    .Listen("tcp://127.0.0.1:6202")
                    .AddActorTransferAdapter<TestActor, TestActorTransferAdapter>("warrior").ChannelName("actor-node-b");
            }));

        Assert.Contains("Duplicate actor transfer 'warrior'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsTransferAdaptersWithoutFactoriesOnTheSameNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.ActorTransferTimeout = TimeSpan.FromSeconds(15);
            options.ActorTransferForwardWindow = TimeSpan.FromSeconds(5);
            options.AddRouteMesh("actor-node-a")
                .Listen("tcp://127.0.0.1:6203")
                .AddActorTransferAdapter<TestActor, TestActorTransferAdapter>("warrior").ChannelName("actor-node-a");
        });

        var registration = services.BuildServiceProvider()
            .GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.True(registration.ActorCatalog.TryGetTransfer("warrior", out _));
    }

    [Fact]
    public void SessionActorDispatch_Registers_Stream_Node_Without_Explicit_Relay_Target()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var mesh = options.AddRouteMesh("actor-node");
                mesh.ChannelName("actor-node");
                {
                    var spot = mesh;
                    {
                        var router = spot.Listen("tcp://127.0.0.1:7302");
                    }
                }
            }
            {
                var stream = options.AddStreamNode("stream.node");
                stream.Bind("tcp://127.0.0.1:9100");
                stream.AddSession<TestHeaderSession>();
            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();

        Assert.Single(registration.StreamNodes);
        Assert.Single(registration.SpotNodes);
    }

    [Fact]
    public void AddZLinkFramework_Uses_Standard_DI_For_Application_Dependencies()
    {
        var services = new ServiceCollection();
        services.AddScoped<ITestSessionDependencyHandler, TestSessionDependencyHandler>();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var stream = options.AddStreamNode("client.stream");
                stream.Bind("tcp://127.0.0.1:9100");
                stream.AddSession<TestSessionWithEnumerableHandlers>();
            }
            {
                var mesh = options.AddRouteMesh("stage-node");
                mesh.ChannelName("stage-node");
                {
                    var spot = mesh;
                    {
                        var router = spot.Listen("tcp://127.0.0.1:6204");
                    }
                    spot.AddSpotFactory<TestSpot>();
                    spot.AddEntrySpot<TestEntrySpot>();
                }
            }
        });

        Assert.DoesNotContain(services,
            static service => service.ServiceType == typeof(TestSessionWithEnumerableHandlers));
        Assert.Contains(services, static service => service.ServiceType == typeof(TestSpot));
        Assert.Contains(services, static service => service.ServiceType == typeof(TestEntrySpot));

        using var provider = services.BuildServiceProvider();
        Assert.IsType<TestSessionDependencyHandler>(
            Assert.Single(provider.GetServices<ITestSessionDependencyHandler>()));
    }

    [Fact]
    public async Task SessionHandlerRegistry_Handles_ManuallyRegistered_Typed_Handler()
    {
        var services = new ServiceCollection();
        services.AddScoped<TestSessionPacketHandler>();

        using var provider = services.BuildServiceProvider();
        await using var scope = provider.CreateAsyncScope();
        await using var handlerInstances = new ZLinkScopedHandlerInstanceOwner(scope.ServiceProvider);
        var context = new TestSessionPacketContext();
        var registry = new ZLinkSessionHandlerRegistry(handlerInstances);
        registry.BindContext(context);
        registry.AddHandler<TestSessionPacketHandler>();
        registry.Bind();

        var handled = await registry.TryHandleAsync(
            new ZLinkSessionDispatchContext(nameof(TestSessionPacketMessage)),
            ZLinkMessage.From(new TestSessionPacketMessage()));
        var unhandled = await registry.TryHandleAsync(
            new ZLinkSessionDispatchContext("test.unhandled"),
            ZLinkMessage.From(new TestSessionPacketMessage()));

        Assert.True(handled);
        Assert.False(unhandled);
        Assert.Equal(1, context.HandledCount);
    }

    [Fact]
    public async Task SessionHandlerRegistry_Reuses_Unregistered_Handler_And_Disposes_It_On_Disconnect()
    {
        var lifetime = new AsyncSessionHandlerLifetime();
        using var provider = new ServiceCollection()
            .AddSingleton(lifetime)
            .BuildServiceProvider();
        var handlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        var context = new TestSessionPacketContext();
        var registry = new ZLinkSessionHandlerRegistry(handlerInstances);
        registry.BindContext(context);
        registry.AddHandler<AsyncDisposableSessionPacketHandler>();
        registry.Bind();

        await registry.TryHandleAsync(
            new ZLinkSessionDispatchContext(nameof(AsyncDisposableSessionPacketMessage)),
            ZLinkMessage.From(new AsyncDisposableSessionPacketMessage()));
        await registry.TryHandleAsync(
            new ZLinkSessionDispatchContext(nameof(AsyncDisposableSessionPacketMessage)),
            ZLinkMessage.From(new AsyncDisposableSessionPacketMessage()));

        Assert.Equal(2, lifetime.Invocations.Count);
        Assert.Same(lifetime.Invocations[0], lifetime.Invocations[1]);
        await handlerInstances.DisposeAsync();
        await handlerInstances.DisposeAsync();
        Assert.Equal(1, lifetime.DisposeCount);
    }

    [Fact]
    public async Task SessionHandlerRegistry_AutoRegisters_Compatible_Typed_Handlers_From_Assembly()
    {
        using var provider = new ServiceCollection().BuildServiceProvider();
        await using var handlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        var context = new TestSessionPacketContext();
        var registry = new ZLinkSessionHandlerRegistry(handlerInstances);
        registry.BindContext(context);
        registry.AddScannedHandlers(ZLinkScannedSessionHandlerScanner.Scan(
            typeof(TestSessionPacketHandler).Assembly,
            new HashSet<Type>()));
        registry.Bind();

        var handled = await registry.TryHandleAsync(
            new ZLinkSessionDispatchContext(nameof(TestSessionPacketMessage)),
            ZLinkMessage.From(new TestSessionPacketMessage()));

        Assert.True(handled);
        Assert.Equal(1, context.HandledCount);
    }

    [Fact]
    public async Task SessionHandlerRegistry_Invokes_Attributed_Method_On_Active_Session()
    {
        using var provider = new ServiceCollection().BuildServiceProvider();
        await using var handlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        var context = new TestSessionPacketContext();
        var session = new TestHeaderSession(context);
        var registry = new ZLinkSessionHandlerRegistry(handlerInstances);
        registry.BindContext(context);
        registry.BindSession(session);
        registry.AddScannedHandlers(ZLinkScannedSessionHandlerScanner.Scan(
            typeof(TestHeaderSession).Assembly,
            new HashSet<Type> { typeof(TestHeaderSession) }));
        registry.Bind();

        var handled = await registry.TryHandleAsync(
            new ZLinkSessionDispatchContext(nameof(AttributedSessionPacketMessage)),
            ZLinkMessage.From(new AttributedSessionPacketMessage()));

        Assert.True(handled);
        Assert.Equal(1, session.AttributedHandledCount);
    }

    [Fact]
    public void Registration_Includes_Registered_Application_Assemblies_In_Handler_Scan_By_Default()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddStreamNode("client.stream")
                .Bind("tcp://127.0.0.1:9100")
                .AddSession<TestHeaderSession>();
            options.AddRouteMesh("stage-node")
                .Listen("tcp://127.0.0.1:9000")
                .AddSpotFactory<TestSpot>()
                .AddEntrySpot<TestEntrySpot>().ChannelName("stage-node");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        var assemblies = registration.EnumerateHandlerScanAssemblies().ToArray();

        Assert.Contains(typeof(TestHeaderSession).Assembly, assemblies);
        Assert.Contains(typeof(TestSpot).Assembly, assemblies);
        Assert.Contains(typeof(TestEntrySpot).Assembly, assemblies);
        Assert.Contains(registration.ScannedHandlerCatalog.SessionHandlers, static candidate =>
            candidate is ZLinkScannedAttributedSessionHandler attributed
            && attributed.SessionType == typeof(TestHeaderSession));
    }

    [Fact]
    public async Task FrozenSessionHandlerCatalogCanBindRepeatedRegistriesWithoutRescanning()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.AddStreamNode("client.stream")
                .Bind("tcp://127.0.0.1:9100")
                .AddSession<TestHeaderSession>();
        });
        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var candidates = registration.ScannedHandlerCatalog.SessionHandlers;

        var firstContext = new TestSessionPacketContext();
        await using var firstHandlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        var first = new ZLinkSessionHandlerRegistry(firstHandlerInstances);
        first.BindContext(firstContext);
        first.AddScannedHandlers(candidates);
        first.Bind();

        var secondContext = new TestSessionPacketContext();
        await using var secondHandlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        var second = new ZLinkSessionHandlerRegistry(secondHandlerInstances);
        second.BindContext(secondContext);
        second.AddScannedHandlers(candidates);
        second.Bind();

        Assert.Same(candidates, registration.ScannedHandlerCatalog.SessionHandlers);
        Assert.True(await first.TryHandleAsync(
            new ZLinkSessionDispatchContext(nameof(TestSessionPacketMessage)),
            ZLinkMessage.From(new TestSessionPacketMessage())));
        Assert.True(await second.TryHandleAsync(
            new ZLinkSessionDispatchContext(nameof(TestSessionPacketMessage)),
            ZLinkMessage.From(new TestSessionPacketMessage())));
        Assert.Equal(1, firstContext.HandledCount);
        Assert.Equal(1, secondContext.HandledCount);
    }

    [Fact]
    public void Registration_Can_Disable_Implicit_Handler_Auto_Registration()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.DisableImplicitHandlerAutoRegistration();
            options.AddStreamNode("client.stream")
                .Bind("tcp://127.0.0.1:9100")
                .AddSession<TestHeaderSession>();
            options.AddRouteMesh("stage-node")
                .Listen("tcp://127.0.0.1:9000")
                .AddSpotFactory<TestSpot>().ChannelName("stage-node");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();

        Assert.Empty(registration.EnumerateHandlerScanAssemblies());
    }

    [Fact]
    public void Registration_Disabling_Implicit_Auto_Registration_Keeps_Explicit_Handler_Assemblies()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.DisableImplicitHandlerAutoRegistration();
            options.AddHandlersFromAssembly(typeof(TestSessionPacketHandler).Assembly);
            options.AddRouteMesh("stage-node")
                .Listen("tcp://127.0.0.1:9000")
                .AddSpotFactory<TestSpot>().ChannelName("stage-node");
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        var assembly = Assert.Single(registration.EnumerateHandlerScanAssemblies());

        Assert.Equal(typeof(TestSessionPacketHandler).Assembly, assembly);
    }

    [Fact]
    public void SessionHandlerRegistry_Throws_When_PacketName_Is_Duplicated()
    {
        using var provider = new ServiceCollection()
            .AddScoped<DuplicateSessionPacketHandler>()
            .AddScoped<SecondDuplicateSessionPacketHandler>()
            .BuildServiceProvider();
        var handlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        var registry = new ZLinkSessionHandlerRegistry(handlerInstances);
        registry.BindContext(new DuplicateSessionPacketContext());
        registry.AddHandler<DuplicateSessionPacketHandler>();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            registry.AddHandler<SecondDuplicateSessionPacketHandler>());

        Assert.Contains($"Session packet handler '{nameof(DuplicateSessionPacketMessage)}' is already registered",
            exception.Message,
            StringComparison.Ordinal);
    }

    [Fact]
    public async Task AddZLinkFramework_Registers_SpotPublisher_Client()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var mesh = options.AddRouteMesh("stage-node");
                mesh.ChannelName("stage-node");
                {
                    var spot = mesh;
                    {
                        var router = spot.Listen("tcp://127.0.0.1:6204");
                    }
                }
            }
        });

        await using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkSpotPublisherClient>());
    }

    [Fact]
    public void AddZLinkFramework_Registers_BoundSession_Factory()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            var mesh = options.AddRouteMesh("gateway")
                .Listen("tcp://127.0.0.1:6202")
                .SetRoutingId(RoutingId.From("gateway"));
            mesh.ChannelName("gateway");
        });

        using var provider = services.BuildServiceProvider();
    }

    [Fact]
    public async Task AddZLinkFramework_Registers_Internal_ActorResolver_With_LocationStore_Without_ActorFactory()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
        });

        await using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkActorResolver>());
        Assert.Null(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public async Task AddZLinkFramework_AddLocationStores_ResolvesEveryStoreRoleToOneInstance()
    {
        var services = new ServiceCollection();
        var backing = new ZLinkInMemoryLocationStore();

        services.AddZLinkFramework(options =>
        {
            // Extension-package style registration: one physical store
            // instance backs every location store contract, the way codecs
            // register serializer instances.
            options.AddLocationStore(backing);
        });

        await using var provider = services.BuildServiceProvider();

        Assert.Same(backing, provider.GetRequiredService<IZLinkMeshNodeLocationStore>());
        Assert.Same(backing, provider.GetRequiredService<IZLinkSpotLocationStore>());
        Assert.Same(backing, provider.GetRequiredService<IZLinkActorLocationStore>());
        Assert.Same(backing, provider.GetRequiredService<IZLinkOwnerLeaseStore>());
        Assert.Same(backing, provider.GetRequiredService<IZLinkLocationChangeStampStore>());

        // The location runtime surface comes up on top of the hook exactly
        // as it does for the per-role registrations.
        Assert.NotNull(provider.GetService<IZLinkLocationRuntimeQuery>());
        Assert.NotNull(provider.GetService<IZLinkMeshNodeLocationResolver>());
        Assert.Same(
            provider.GetRequiredService<ZLinkLocationLifecycle>().ActorOwnership,
            provider.GetRequiredService<IZLinkActorLocationLifecycle>());
    }

    [Fact]
    public async Task AddZLinkFramework_RegistersOneAllocatedRoutingIdCapabilityForAllBuilders()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMesh("events")
                .Listen("inproc://allocated-events")
                .UseAllocatedRoutingId(10)
                .SetRoutingIdAllocationGroup("node")
                .ChannelName("events");
            options.AddRouteMesh("spot")
                .Listen("inproc://allocated-spot")
                .UseAllocatedRoutingId(10)
                .SetRoutingIdAllocationGroup("node").ChannelName("spot");
        });

        await using var provider = services.BuildServiceProvider();
        Assert.Same(
            provider.GetRequiredService<ZLinkInMemoryLocationStore>(),
            provider.GetRequiredService<IZLinkRoutingIdSlotAllocationStore>());
        Assert.Same(
            provider.GetRequiredService<ZLinkAllocatedRoutingIdRuntime>(),
            provider.GetRequiredService<IZLinkAllocatedRoutingIdProvider>());
    }

    [Fact]
    public void AllocatedRoutingIdPolicy_UsesTheContractDefaults()
    {
        var options = new ZLinkLocationOptions();

        Assert.Equal(TimeSpan.FromSeconds(10), options.HeartbeatInterval);
        Assert.Equal(TimeSpan.FromSeconds(30), options.OwnerLeaseTtl);
        Assert.Equal(TimeSpan.FromSeconds(5), options.RoutingIdFencingMargin);
        Assert.Equal(TimeSpan.FromSeconds(3), options.OwnerLeaseRenewTimeout);
    }

    [Fact]
    public void AddZLinkFramework_RejectsInvalidAllocatedRoutingIdConfigurations()
    {
        var missingStore = Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(options =>
                options.AddRouteMesh("events")
                    .Listen("inproc://allocated-no-store")
                    .UseAllocatedRoutingId(10)
                    .ChannelName("events")));
        Assert.Contains("require", missingStore.Message, StringComparison.OrdinalIgnoreCase);

        var storeWithoutCapability = DispatchProxy.Create<ITrackedLocationStore, TrackedLocationStoreProxy>();
        var missingCapability = Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(options =>
            {
                options.AddLocationStore(storeWithoutCapability);
                options.AddRouteMesh("events")
                    .Listen("inproc://allocated-no-capability")
                    .UseAllocatedRoutingId(10)
                    .ChannelName("events");
            }));
        Assert.Contains("does not provide", missingCapability.Message, StringComparison.Ordinal);

        Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddRouteMesh("events")
                    .Listen("inproc://allocated-fixed")
                    .SetRoutingId(RoutingId.From("fixed"))
                    .UseAllocatedRoutingId(10)
                    .ChannelName("events");
            }));

        Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddRouteMesh("events")
                    .Listen("inproc://allocated-fixed-reversed")
                    .UseAllocatedRoutingId(10)
                    .SetRoutingId(RoutingId.From("fixed"))
                    .ChannelName("events");
            }));

        Assert.Throws<ZLinkConfigurationException>(() =>
            new ServiceCollection().AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddRouteMesh("events")
                    .Listen("inproc://allocated-group-only")
                    .SetRoutingIdAllocationGroup("group")
                    .ChannelName("events");
            }));
    }

    [Fact]
    public async Task HostStartup_AllocatesRoutingIdBeforeBindingAndPublishesReadyResult()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMesh("events")
                .Listen($"inproc://allocated-ready-{Guid.NewGuid():N}")
                .UseAllocatedRoutingId(2)
                .ChannelName("events");
        });
        using var host = builder.Build();

        await host.StartAsync();
        var allocation = await host.Services
            .GetRequiredService<IZLinkAllocatedRoutingIdProvider>()
            .WaitForReadyAllocationAsync("events");

        Assert.Equal(1, allocation.Slot);
        Assert.Equal(RoutingId.From("events1"), allocation.MemberRoutingIds["events"]);
        await host.StopAsync();
    }

    [Fact]
    public async Task AllocatedRoutingIdRuntime_RetriesTransientStoreFailureWithTheSameOwner()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.ConfigureLocations().PollingInterval = TimeSpan.FromMilliseconds(1);
            options.AddRouteMesh("events")
                .Listen($"inproc://allocated-retry-{Guid.NewGuid():N}")
                .UseAllocatedRoutingId(2)
                .ChannelName("events");
        });
        await using var provider = services.BuildServiceProvider();
        var locations = provider.GetRequiredService<ZLinkLocationRuntime>();
        var innerStore = provider.GetRequiredService<IZLinkRoutingIdSlotAllocationStore>();
        var flakyStore = new FailOnceRoutingIdSlotStore(innerStore);
        var allocation = new ZLinkAllocatedRoutingIdRuntime(
            provider.GetRequiredService<ZLinkFrameworkRegistration>(),
            flakyStore,
            locations,
            provider.GetRequiredService<ZLinkLocationOptions>());

        await locations.StartAsync(RoutingId.From("retry-node"));
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            await allocation.StartAsync(timeout.Token);

            Assert.Equal(2, flakyStore.AcquireAttempts);
            Assert.Single((await innerStore.ListRoutingIdSlotsAsync("events")).Allocations);
        }
        finally
        {
            await allocation.StopAsync(CancellationToken.None);
            await locations.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenAddLocationStoreIsCombinedWithInMemoryStore()
    {
        var inMemory = new ServiceCollection();
        var inMemoryConflict = Assert.Throws<ZLinkConfigurationException>(() =>
            inMemory.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                options.AddLocationStore(new ZLinkInMemoryLocationStore());
            }));
        Assert.Contains("AddLocationStore", inMemoryConflict.Message, StringComparison.Ordinal);

    }

    [Fact]
    public async Task AddLocationStore_Instance_Is_Disposed_Exactly_Once_By_The_Host_Provider()
    {
        var store = DispatchProxy.Create<ITrackedLocationStore, TrackedLocationStoreProxy>();
        var tracker = (TrackedLocationStoreProxy)(object)store;
        var services = new ServiceCollection();
        services.AddZLinkFramework(options => options.AddLocationStore(store));
        var provider = services.BuildServiceProvider();

        Assert.Same(store, provider.GetRequiredService<IZLinkLocationStore>());
        Assert.Same(store, provider.GetRequiredService<IZLinkOwnerLeaseStore>());
        _ = provider.GetServices<IHostedService>().ToArray();
        await provider.DisposeAsync();

        Assert.Equal(1, tracker.DisposeCount);
    }

    [Fact]
    public async Task HostStartup_Rejects_Duplicate_UserSpot_Packet_Registration()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.DisableImplicitHandlerAutoRegistration();
            options.AddRouteMesh("duplicate-packet")
                .Listen($"inproc://duplicate-packet-{Guid.NewGuid():N}")
                .AddSpotFactory<DuplicatePacketSpot>().ChannelName("duplicate-packet");
        });
        using var host = builder.Build();

        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());

        Assert.Contains(
            $"SPOT packet handler '{nameof(DuplicatePacketMessage)}' is already registered",
            exception.Message,
            StringComparison.Ordinal);
    }

    [Fact]
    public async Task HostStartup_Rejects_Duplicate_UserSpot_Subscription_Registration()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.DisableImplicitHandlerAutoRegistration();
            options.AddRouteMesh("duplicate-subscription")
                .Listen($"inproc://duplicate-subscription-{Guid.NewGuid():N}")
                .AddSpotFactory<DuplicateSubscriptionSpot>().ChannelName("duplicate-subscription");
        });
        using var host = builder.Build();

        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());

        Assert.Contains("SPOT subscription handler", exception.Message, StringComparison.Ordinal);
        Assert.Contains("already registered", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task HostStartup_Rejects_Duplicate_UserSpot_Actor_Registration()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.DisableImplicitHandlerAutoRegistration();
            options.AddRouteMesh("duplicate-actor")
                .Listen($"inproc://duplicate-actor-{Guid.NewGuid():N}")
                .AddSpotFactory<DuplicateActorSpot>().ChannelName("duplicate-actor");
        });
        using var host = builder.Build();

        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());

        Assert.Contains("actor packet", exception.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("already registered", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task HostStartup_Rejects_Invalid_ScannedSpotTimer()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMesh("invalid-timer")
                .Listen($"inproc://invalid-timer-{Guid.NewGuid():N}")
                .AddSpotFactory<InvalidTimerSpot>().ChannelName("invalid-timer");
        });
        using var host = builder.Build();

        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());

        Assert.Contains("SPOT timer period must be greater than zero", exception.Message, StringComparison.Ordinal);
    }

    private sealed record DuplicatePacketMessage(string Value);

    private sealed class DuplicatePacketSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddPacket<FirstDuplicatePacketHandler>();
            Context.Handlers.AddPacket<SecondDuplicatePacketHandler>();
        }
    }

    private sealed class FirstDuplicatePacketHandler
        : IZLinkSpotPacketHandler<DuplicatePacketSpot, DuplicatePacketMessage>
    {
        public ValueTask HandleAsync(
            DuplicatePacketSpot spot,
            DuplicatePacketMessage message,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed class SecondDuplicatePacketHandler
        : IZLinkSpotPacketHandler<DuplicatePacketSpot, DuplicatePacketMessage>
    {
        public ValueTask HandleAsync(
            DuplicatePacketSpot spot,
            DuplicatePacketMessage message,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed record DuplicateSubscriptionMessage(string Value);

    private sealed class DuplicateSubscriptionSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddSubscribe<DuplicateSubscriptionHandler>("duplicate-channel", "duplicate-topic");
            Context.Handlers.AddSubscribe<DuplicateSubscriptionHandler>("duplicate-channel", "duplicate-topic");
        }
    }

    private sealed class DuplicateSubscriptionHandler
        : IZLinkSpotSubscriptionHandler<DuplicateSubscriptionSpot, DuplicateSubscriptionMessage>
    {
        public ValueTask HandleAsync(
            DuplicateSubscriptionSpot spot,
            DuplicateSubscriptionMessage message,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed record DuplicateActorMessage(string Value);

    private sealed class DuplicateActor(string actorId, IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;
    }

    private sealed class DuplicateActorSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddActorPacket<DuplicateActorHandler, DuplicateActor>();
            Context.Handlers.AddActorPacket<SecondDuplicateActorHandler, DuplicateActor>();
        }
    }

    private sealed class DuplicateActorHandler
        : IZLinkSpotActorSendHandler<DuplicateActorSpot, DuplicateActor, DuplicateActorMessage>
    {
        public ValueTask HandleAsync(
            DuplicateActorSpot spot,
            DuplicateActor actor,
            ZLinkSpotActorSendContext context,
            DuplicateActorMessage message,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed class SecondDuplicateActorHandler
        : IZLinkSpotActorSendHandler<DuplicateActorSpot, DuplicateActor, DuplicateActorMessage>
    {
        public ValueTask HandleAsync(
            DuplicateActorSpot spot,
            DuplicateActor actor,
            ZLinkSpotActorSendContext context,
            DuplicateActorMessage message,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed class InvalidTimerSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }

    [ZLinkSpotTimerHandler("invalid", 0)]
    private sealed class InvalidTimerHandler : IZLinkSpotTimerHandler<InvalidTimerSpot>
    {
        public ValueTask HandleAsync(
            InvalidTimerSpot spot,
            ZLinkTimerTick tick,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private interface ITrackedLocationStore : IZLinkLocationStore, IAsyncDisposable;

    private class TrackedLocationStoreProxy : DispatchProxy
    {
        private readonly ZLinkInMemoryLocationStore _inner = new();

        public int DisposeCount { get; private set; }

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            Assert.NotNull(targetMethod);
            if (targetMethod.DeclaringType == typeof(IAsyncDisposable))
            {
                DisposeCount++;
                return ValueTask.CompletedTask;
            }

            return targetMethod.Invoke(_inner, args);
        }
    }

    private sealed class FailOnceRoutingIdSlotStore(IZLinkRoutingIdSlotAllocationStore inner)
        : IZLinkRoutingIdSlotAllocationStore
    {
        private int _acquireAttempts;

        public int AcquireAttempts => Volatile.Read(ref _acquireAttempts);

        public ValueTask<ZLinkRoutingIdSlotAcquireResult> AcquireRoutingIdSlotAsync(
            ZLinkRoutingIdSlotAcquireRequest request,
            CancellationToken cancellationToken = default)
        {
            if (Interlocked.Increment(ref _acquireAttempts) == 1)
                throw new IOException("transient allocation store failure");
            return inner.AcquireRoutingIdSlotAsync(request, cancellationToken);
        }

        public ValueTask<ZLinkRoutingIdSlotReleaseResult> ReleaseRoutingIdSlotAsync(
            string groupName,
            int slot,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.ReleaseRoutingIdSlotAsync(groupName, slot, owner, cancellationToken);

        public ValueTask<ZLinkRoutingIdSlotAllocationSnapshot> ListRoutingIdSlotsAsync(
            string groupName,
            CancellationToken cancellationToken = default) =>
            inner.ListRoutingIdSlotsAsync(groupName, cancellationToken);
    }
}
