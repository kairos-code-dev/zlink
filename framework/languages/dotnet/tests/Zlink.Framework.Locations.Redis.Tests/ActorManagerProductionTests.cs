using System.Net;
using System.Net.Sockets;
using System.Reflection;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class ActorManagerProductionTests
{
    [Fact]
    public async Task CapacityRaceRemovesFirstCandidateAndExecutesSecondNodeCallback()
    {
        TestActorFactory.Reset();
        var inner = new ZLinkInMemoryLocationStore();
        var (store, capacityRace) = CapacityRaceLocationStore.Create(inner);
        var suffix = Guid.NewGuid().ToString("N");
        var firstRid = RoutingId.From($"actor-a-{suffix}");
        var secondRid = RoutingId.From($"actor-b-{suffix}");
        var sourceRid = RoutingId.From($"actor-source-{suffix}");
        var firstEndpoint = $"tcp://127.0.0.1:{FindFreeTcpPort()}";
        var secondEndpoint = $"tcp://127.0.0.1:{FindFreeTcpPort()}";
        var sourceEndpoint = $"tcp://127.0.0.1:{FindFreeTcpPort()}";

        await using var firstProvider = BuildServer(
            store, firstRid, firstEndpoint);
        await using var secondProvider = BuildServer(
            store, secondRid, secondEndpoint);
        await using var sourceProvider = BuildClient(
            store,
            sourceRid,
            sourceEndpoint,
            (firstRid, firstEndpoint),
            (secondRid, secondEndpoint));
        var first = firstProvider.GetRequiredService<ZLinkFrameworkRuntime>();
        var second = secondProvider.GetRequiredService<ZLinkFrameworkRuntime>();
        var source = sourceProvider.GetRequiredService<ZLinkFrameworkRuntime>();
        await first.StartAsync(CancellationToken.None);
        await second.StartAsync(CancellationToken.None);
        await source.StartAsync(CancellationToken.None);
        try
        {
            await PublishServerDescriptorAsync(
                inner,
                first,
                firstRid,
                firstEndpoint);
            await PublishServerDescriptorAsync(
                inner,
                second,
                secondRid,
                secondEndpoint);
            await WaitUntilAsync(() =>
                source.GetSpotNodeRuntime("objects").Node.MeshStatus()
                    .AdmittedPeerCount == 2);

            using var timeout = new CancellationTokenSource(
                TimeSpan.FromSeconds(10));
            var result = await new ZLinkActorManagerService(source)
                .GetOrCreate($"actor-{suffix}", "player")
                .Async(timeout.Token);

            Assert.IsType<ZLinkActorCreateResult.Created>(result);
            Assert.Equal(2, capacityRace.ActorReserveAttempts);
            Assert.Equal(1, TestActorFactory.CreateCount);
            Assert.Equal(1, TestEntrySpot.CreateCount);
        }
        finally
        {
            await source.StopAsync(CancellationToken.None);
            await second.StopAsync(CancellationToken.None);
            await first.StopAsync(CancellationToken.None);
        }
    }

    [Fact]
    public async Task SameProcessServerExecutesLocalProductionActorTarget()
    {
        TestActorFactory.Reset();
        var store = new ZLinkInMemoryLocationStore();
        var suffix = Guid.NewGuid().ToString("N");
        var rid = RoutingId.From($"actor-local-{suffix}");
        var endpoint = $"tcp://127.0.0.1:{FindFreeTcpPort()}";
        await using var provider = BuildServer(store, rid, endpoint);
        var runtime = provider.GetRequiredService<ZLinkFrameworkRuntime>();
        Assert.NotNull(provider.GetService<TestActorFactory>());
        await runtime.StartAsync(CancellationToken.None);
        try
        {
            await PublishServerDescriptorAsync(
                store,
                runtime,
                rid,
                endpoint);

            using var timeout = new CancellationTokenSource(
                TimeSpan.FromSeconds(10));
            var result = Assert.IsType<ZLinkActorCreateResult.Created>(
                await provider.GetRequiredService<IZLinkActorManager>()
                    .GetOrCreate($"local-{suffix}", "player")
                    .Async(timeout.Token));

            Assert.Equal(rid, result.Actor.NodeRid);
            Assert.Equal(1, TestActorFactory.CreateCount);
            Assert.Equal(1, TestEntrySpot.CreateCount);
        }
        finally
        {
            await runtime.StopAsync(CancellationToken.None);
        }
    }

    private static ServiceProvider BuildServer(
        IZLinkLocationStore store,
        RoutingId rid,
        string endpoint)
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(store);
            var node = options.AddRouteMesh("objects")
                .Listen(endpoint)
                .SetRoutingId(rid);
            node.Channel("objects").Server();
            node.Objects().Server()
                .AddEntrySpot<TestEntrySpot>()
                .AddActorFactory<TestActor, TestActorFactory>(
                    "player",
                    null,
                    ZLinkRelocationPolicy<TestActor>.Disabled);
        });
        return services.BuildServiceProvider();
    }

    private static ServiceProvider BuildClient(
        IZLinkLocationStore store,
        RoutingId rid,
        string endpoint,
        params (RoutingId Rid, string Endpoint)[] targets)
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(store);
            var node = options.AddRouteMesh("objects")
                .Listen(endpoint)
                .SetRoutingId(rid);
            node.Channel("objects").Client();
            foreach (var target in targets)
                node.PeerConnections.Connect(target.Rid, target.Endpoint);
            node.Objects().Client();
        });
        return services.BuildServiceProvider();
    }

    private static int FindFreeTcpPort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    private static async Task PublishServerDescriptorAsync(
        IZLinkLocationStore store,
        ZLinkFrameworkRuntime runtime,
        RoutingId rid,
        string endpoint)
    {
        var node = runtime.GetSpotNodeRuntime("objects");
        var entrySpotId = node.Registration.EntrySpotId
                          ?? throw new InvalidOperationException(
                              "The Entry Spot id was not assigned.");
        ZLinkSpotLocation? entry = null;
        await WaitUntilAsync(async () =>
        {
            entry = await store.ResolveSpotAsync(
                new ZLinkSpotLocationKey(entrySpotId));
            return entry is not null;
        });
        Assert.Equal(rid, entry!.OwnerNodeRid);
        var generation = node.Node.MeshStatus().LifecycleGeneration;
        Assert.Equal(generation, entry.OwnerNodeGeneration);
        var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                $"actor-owner-{Guid.NewGuid():N}",
                TimeSpan.FromMinutes(1))).Token;
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateSpotAsync(
                entry with { OwnerId = owner.OwnerId },
                ZLinkLocationWriteIntent.Takeover)).Status);
        var result = await store.UpdateMeshNodeAsync(
            new ZLinkMeshNodeDescriptor(
                "objects",
                rid,
                generation,
                1,
                endpoint,
                new Dictionary<string, int>(StringComparer.Ordinal)
                {
                    ["objects"] = 100
                },
                string.Empty,
                owner.OwnerId,
                owner.LeaseGeneration,
                DateTimeOffset.UtcNow)
            {
                ObjectRole = ZLinkMeshNodeObjectRole.Server,
                State = ZLinkFrameworkRuntimeState.Serving,
                EntrySpotId = entrySpotId,
                PlacementWeight = 100,
                ObjectCapabilities =
                [
                    new ZLinkObjectCapability(
                        ZLinkPlacementObjectKind.Actor,
                        "player",
                        ZLinkObjectMaintenancePolicyKind.Disabled,
                        false,
                        0)
                ],
                Capacity = new ZLinkPlacementCapacity(
                    new ZLinkPopulationCapacity(0, 0, 10),
                    new ZLinkPopulationCapacity(0, 0, 0),
                    Array.Empty<ZLinkSpotTypeCapacity>()),
                ActivationConcurrency = new ZLinkActivationConcurrency(0, 128)
            },
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, result.Status);
    }

    private static async Task WaitUntilAsync(
        Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        while (!condition())
            await Task.Delay(10, timeout.Token);
    }

    private static async Task WaitUntilAsync(
        Func<Task<bool>> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        while (!await condition().ConfigureAwait(false))
            await Task.Delay(10, timeout.Token);
    }

    private sealed class TestActor(
        string actorId,
        IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;
    }

    private sealed class TestActorFactory : IZLinkActorFactory<TestActor>
    {
        private static int _createCount;

        public static int CreateCount => Volatile.Read(ref _createCount);

        public static void Reset()
        {
            Volatile.Write(ref _createCount, 0);
            TestEntrySpot.Reset();
        }

        public ValueTask<TestActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Interlocked.Increment(ref _createCount);
            return ValueTask.FromResult(new TestActor(actorId, context));
        }
    }

    private sealed class TestEntrySpot(IZLinkEntrySpotContext context)
        : IZLinkEntrySpot<TestActor>
    {
        private static int _createCount;

        public IZLinkEntrySpotContext Context { get; } = context;

        public static int CreateCount => Volatile.Read(ref _createCount);

        public static void Reset() => Volatile.Write(ref _createCount, 0);

        public ValueTask<ZLinkActorCreateResponse> OnCreateActorAsync(
            TestActor actor,
            ZLinkMessage createRequest,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Interlocked.Increment(ref _createCount);
            return ValueTask.FromResult(ZLinkActorCreateResponse.Accept());
        }

        public ValueTask OnJoinedActorAsync(
            TestActor actor,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask OnLeaveActorAsync(
            TestActor actor,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private class CapacityRaceLocationStore : DispatchProxy
    {
        private IZLinkLocationStore _inner = null!;
        private int _actorReserveAttempts;

        public int ActorReserveAttempts => Volatile.Read(
            ref _actorReserveAttempts);

        public static (
            IZLinkLocationStore Store,
            CapacityRaceLocationStore Control) Create(
            IZLinkLocationStore inner)
        {
            var contract = DispatchProxy.Create<
                IZLinkLocationStore,
                CapacityRaceLocationStore>();
            var proxy = (CapacityRaceLocationStore)(object)contract;
            proxy._inner = inner;
            return (contract, proxy);
        }

        protected override object? Invoke(
            MethodInfo? targetMethod,
            object?[]? args)
        {
            ArgumentNullException.ThrowIfNull(targetMethod);
            if (targetMethod.Name == nameof(IZLinkAuthorityStore.ReserveAsync)
                && args is { Length: > 0 }
                && args[0] is ZLinkObjectReservationRequest
                {
                    ObjectKind: ZLinkPlacementObjectKind.Actor
                }
                && Interlocked.Increment(ref _actorReserveAttempts) == 1)
                return ValueTask.FromResult<ZLinkObjectReserveResult>(
                    new ZLinkObjectReserveResult.PlacementCapacityExhausted());

            try
            {
                return targetMethod.Invoke(_inner, args);
            }
            catch (TargetInvocationException exception)
                when (exception.InnerException is not null)
            {
                throw exception.InnerException;
            }
        }
    }
}
