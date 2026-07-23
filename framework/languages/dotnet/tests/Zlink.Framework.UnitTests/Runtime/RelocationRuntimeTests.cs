using System.Diagnostics;
using System.Security.Cryptography;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Configuration.Builders;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests;

public sealed class RelocationRuntimeTests
{
    [Fact]
    public async Task Authority_relocation_uses_exact_owner_and_capacity_fence_with_opaque_payloads()
    {
        var store = new ZLinkInMemoryLocationStore();
        var source = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "source-owner",
                TimeSpan.FromMinutes(1)));
        var target = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "target-owner",
                TimeSpan.FromMinutes(1)));
        await store.UpdateMeshNodeAsync(
            AuthorityDescriptor("source", source.Token),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateMeshNodeAsync(
            AuthorityDescriptor("target", target.Token),
            ZLinkLocationWriteIntent.NewClaim);
        var key = new ZLinkAuthorityKey("actor:mesh:actor-1");
        var creating = new byte[] { 0x11, 0x00, 0xff };
        var ready = new byte[] { 0x22, 0x00, 0xfe };
        var reservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                new ZLinkObjectReservationRequest(
                    ZLinkPlacementObjectKind.Actor,
                    key,
                    "Game.Actor",
                    null,
                    null,
                    "intent-1",
                    SHA256.HashData("intent-1"u8),
                    8,
                    new ZLinkMeshNodeDescriptorKey(
                        "mesh",
                        RoutingId.From("source")),
                    1,
                    source.Token,
                    creating,
                    1)));
        var committed = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(reservation.Reservation, ready));
        Assert.Equal(ready, committed.Snapshot.Payload.ToArray());
        Assert.Equal(
            (0L, 1L),
            store.GetPlacementCapacityUsage(
                new ZLinkMeshNodeDescriptorKey(
                    "mesh",
                    RoutingId.From("source")),
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));

        var capacity = Assert.IsType<
            ZLinkRelocationCapacityReserveResult.Reserved>(
            await store.ReserveRelocationCapacityAsync(
                new ZLinkRelocationCapacityReservationRequest(
                    Guid.NewGuid(),
                    key,
                    committed.Snapshot.StoreVersion,
                    ZLinkPlacementObjectKind.Actor,
                    "Game.Actor",
                    new ZLinkMeshNodeDescriptorKey(
                        "mesh",
                        RoutingId.From("source")),
                    1,
                    source.Token,
                    new ZLinkMeshNodeDescriptorKey(
                        "mesh",
                        RoutingId.From("target")),
                    1,
                    target.Token,
                    1)));
        Assert.Equal(
            (1L, 0L),
            store.GetPlacementCapacityUsage(
                new ZLinkMeshNodeDescriptorKey(
                    "mesh",
                    RoutingId.From("target")),
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));
        var opaque = new byte[] { 0xde, 0xad, 0x00, 0xbe, 0xef };
        var moved = Assert.IsType<ZLinkAuthorityCompareExchangeResult.Stored>(
            await store.CompareExchangeAuthorityAsync(
                key,
                committed.Snapshot.StoreVersion,
                new ZLinkAuthorityMutation.Put(
                    opaque,
                    ZLinkAuthorityGenerationTransition.NewOwner,
                    target.Token,
                    capacity.Fence)));

        Assert.Equal(opaque, moved.Snapshot.Payload.ToArray());
        Assert.Equal(target.Token.OwnerId, moved.Snapshot.OwnerId);
        Assert.Equal(
            target.Token.LeaseGeneration,
            moved.Snapshot.OwnerLeaseGeneration);
        Assert.Equal(
            ZLinkRelocationCapacityAbortResult.AlreadyCommitted,
            await store.AbortRelocationCapacityAsync(capacity.Fence));
        Assert.Equal(
            (0L, 0L),
            store.GetPlacementCapacityUsage(
                new ZLinkMeshNodeDescriptorKey(
                    "mesh",
                    RoutingId.From("source")),
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));
        Assert.Equal(
            (0L, 1L),
            store.GetPlacementCapacityUsage(
                new ZLinkMeshNodeDescriptorKey(
                    "mesh",
                    RoutingId.From("target")),
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));
    }

    [Fact]
    public async Task Creation_abort_and_delete_release_exact_capacity_bucket()
    {
        var store = new ZLinkInMemoryLocationStore();
        var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "capacity-owner",
                TimeSpan.FromMinutes(1)));
        var descriptor = new ZLinkMeshNodeDescriptorKey(
            "mesh",
            RoutingId.From("capacity-node"));
        await store.UpdateMeshNodeAsync(
            AuthorityDescriptor("capacity-node", owner.Token),
            ZLinkLocationWriteIntent.NewClaim);

        var abortedReservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                ObjectReservation(
                    "actor:mesh:aborted",
                    descriptor,
                    owner.Token,
                    capacityDelta: 3)));
        Assert.Equal(
            (3L, 0L),
            store.GetPlacementCapacityUsage(
                descriptor,
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));
        Assert.IsType<ZLinkObjectAbortResult.Aborted>(
            await store.AbortAsync(abortedReservation.Reservation));
        Assert.Equal(
            (0L, 0L),
            store.GetPlacementCapacityUsage(
                descriptor,
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));

        var committedReservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                ObjectReservation(
                    "actor:mesh:deleted",
                    descriptor,
                    owner.Token,
                    capacityDelta: 5)));
        var committed = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                committedReservation.Reservation,
                new byte[] { 0x44 }));
        Assert.Equal(
            (0L, 5L),
            store.GetPlacementCapacityUsage(
                descriptor,
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));
        Assert.IsType<ZLinkAuthorityCompareExchangeResult.Deleted>(
            await store.CompareExchangeAuthorityAsync(
                new ZLinkAuthorityKey("actor:mesh:deleted"),
                committed.Snapshot.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));
        Assert.Equal(
            (0L, 0L),
            store.GetPlacementCapacityUsage(
                descriptor,
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));
    }

    [Fact]
    public async Task Creation_admission_checks_profile_and_node_and_type_limits()
    {
        var store = new ZLinkInMemoryLocationStore();
        var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "bounded-owner",
                TimeSpan.FromMinutes(1)));
        var descriptorKey = new ZLinkMeshNodeDescriptorKey(
            "mesh",
            RoutingId.From("bounded-node"));
        await store.UpdateMeshNodeAsync(
            AuthorityDescriptor(
                "bounded-node",
                owner.Token,
                activeLimit: 1,
                pendingLimit: 1,
                placementProfiles: new HashSet<string>(
                    ["premium"],
                    StringComparer.Ordinal)),
            ZLinkLocationWriteIntent.NewClaim);

        Assert.IsType<ZLinkObjectReserveResult.Conflict>(
            await store.ReserveAsync(
                ObjectReservation(
                    "actor:mesh:wrong-profile",
                    descriptorKey,
                    owner.Token,
                    placementProfile: "standard")));

        var first = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                ObjectReservation(
                    "actor:mesh:first",
                    descriptorKey,
                    owner.Token,
                    placementProfile: "premium")));
        var pendingDescriptor = Assert.Single(
            await store.ListMeshNodesAsync("mesh"),
            value => value.Rid == RoutingId.From("bounded-node"));
        Assert.Equal(0, pendingDescriptor.Capacity.Active);
        Assert.Equal(1, pendingDescriptor.Capacity.Pending);
        Assert.IsType<ZLinkObjectReserveResult.PlacementCapacityExhausted>(
            await store.ReserveAsync(
                ObjectReservation(
                    "actor:mesh:pending-overflow",
                    descriptorKey,
                    owner.Token,
                    placementProfile: "premium")));
        Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(first.Reservation, new byte[] { 0x01 }));
        var activeDescriptor = Assert.Single(
            await store.ListMeshNodesAsync("mesh"),
            value => value.Rid == RoutingId.From("bounded-node"));
        Assert.Equal(1, activeDescriptor.Capacity.Active);
        Assert.Equal(0, activeDescriptor.Capacity.Pending);
        Assert.IsType<ZLinkObjectReserveResult.PlacementCapacityExhausted>(
            await store.ReserveAsync(
                ObjectReservation(
                    "actor:mesh:active-overflow",
                    descriptorKey,
                    owner.Token,
                    placementProfile: "premium")));
    }

    [Fact]
    public async Task Descriptor_renew_requires_exact_fence_revision_and_immutable_fields()
    {
        var store = new ZLinkInMemoryLocationStore();
        var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "descriptor-owner",
                TimeSpan.FromMinutes(1)));
        var descriptor = AuthorityDescriptor(
            "descriptor-node",
            owner.Token);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                descriptor,
                ZLinkLocationWriteIntent.NewClaim)).Status);

        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            (await store.UpdateMeshNodeAsync(
                descriptor with { PlacementWeight = 75 },
                ZLinkLocationWriteIntent.Renew)).Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            (await store.UpdateMeshNodeAsync(
                descriptor with
                {
                    DescriptorRevision = 2,
                    ApplicationVersion = 2
                },
                ZLinkLocationWriteIntent.Renew)).Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                descriptor with
                {
                    DescriptorRevision = 2,
                    PlacementWeight = 75
                },
                ZLinkLocationWriteIntent.Renew)).Status);
        var current = Assert.Single(
            await store.ListMeshNodesAsync("mesh"),
            value => value.Rid == RoutingId.From("descriptor-node"));
        Assert.Equal(2UL, current.DescriptorRevision);
        Assert.Equal(75, current.PlacementWeight);
    }

    [Fact]
    public async Task Relocation_admission_checks_target_capability_and_capacity()
    {
        var store = new ZLinkInMemoryLocationStore();
        var source = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "relocation-source-owner",
                TimeSpan.FromMinutes(1)));
        var target = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "relocation-target-owner",
                TimeSpan.FromMinutes(1)));
        var sourceDescriptor = new ZLinkMeshNodeDescriptorKey(
            "mesh",
            RoutingId.From("relocation-source"));
        var targetDescriptor = new ZLinkMeshNodeDescriptorKey(
            "mesh",
            RoutingId.From("relocation-target"));
        await store.UpdateMeshNodeAsync(
            AuthorityDescriptor("relocation-source", source.Token),
            ZLinkLocationWriteIntent.NewClaim);
        var unavailableTarget = AuthorityDescriptor(
                "relocation-target",
                target.Token,
                activeLimit: 1,
                pendingLimit: 1) with
        {
            PlacementWeight = 0
        };
        await store.UpdateMeshNodeAsync(
            unavailableTarget,
            ZLinkLocationWriteIntent.NewClaim);

        var sourceReservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                ObjectReservation(
                    "actor:mesh:relocate",
                    sourceDescriptor,
                    source.Token)));
        var sourceReady = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                sourceReservation.Reservation,
                new byte[] { 0x01 }));
        var relocation = RelocationReservation(
            sourceReady.Snapshot,
            sourceDescriptor,
            source.Token,
            targetDescriptor,
            target.Token);
        Assert.IsType<ZLinkRelocationCapacityReserveResult.TargetUnavailable>(
            await store.ReserveRelocationCapacityAsync(relocation));

        await store.UpdateMeshNodeAsync(
            unavailableTarget with
            {
                DescriptorRevision = 2,
                PlacementWeight = 100
            },
            ZLinkLocationWriteIntent.Renew);
        var occupyingReservation =
            Assert.IsType<ZLinkObjectReserveResult.Reserved>(
                await store.ReserveAsync(
                    ObjectReservation(
                        "actor:mesh:target-occupied",
                        targetDescriptor,
                        target.Token)));
        Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                occupyingReservation.Reservation,
                new byte[] { 0x02 }));

        Assert.IsType<
            ZLinkRelocationCapacityReserveResult.PlacementCapacityExhausted>(
            await store.ReserveRelocationCapacityAsync(
                relocation with { ReservationId = Guid.NewGuid() }));
    }

    [Fact]
    public async Task Aggregate_prepare_is_idempotent_only_for_exact_canonical_request()
    {
        var store = new ZLinkInMemoryLocationStore();
        var source = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "aggregate-source",
                TimeSpan.FromMinutes(1)));
        var target = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "aggregate-target",
                TimeSpan.FromMinutes(1)));
        var sourceDescriptor = new ZLinkMeshNodeDescriptorKey(
            "mesh",
            RoutingId.From("aggregate-source"));
        var targetDescriptor = new ZLinkMeshNodeDescriptorKey(
            "mesh",
            RoutingId.From("aggregate-target"));
        await store.UpdateMeshNodeAsync(
            AuthorityDescriptor("aggregate-source", source.Token),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateMeshNodeAsync(
            AuthorityDescriptor("aggregate-target", target.Token),
            ZLinkLocationWriteIntent.NewClaim);

        var key = new ZLinkAuthorityKey("actor:mesh:aggregate-1");
        var creation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                ObjectReservation(
                    key.Value,
                    sourceDescriptor,
                    source.Token,
                    capacityDelta: 7)));
        var ready = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                creation.Reservation,
                new byte[] { 0x01 }));
        var capacity = Assert.IsType<
            ZLinkRelocationCapacityReserveResult.Reserved>(
            await store.ReserveRelocationCapacityAsync(
                new ZLinkRelocationCapacityReservationRequest(
                    Guid.NewGuid(),
                    key,
                    ready.Snapshot.StoreVersion,
                    ZLinkPlacementObjectKind.Actor,
                    "Game.Actor",
                    sourceDescriptor,
                    1,
                    source.Token,
                    targetDescriptor,
                    1,
                    target.Token,
                    7)));
        var participant = new ZLinkAggregateParticipant(
            key,
            ready.Snapshot.StoreVersion,
            ZLinkAuthorityGenerationTransition.NewOwner,
            new byte[] { 0x02 },
            new byte[] { 0x03 });
        var request = new ZLinkAggregatePrepareRequest(
            Guid.NewGuid(),
            1,
            [participant],
            Enumerable.Repeat((byte)0x5a, 32).ToArray(),
            [capacity.Fence],
            target.Token);

        var prepared = Assert.IsType<ZLinkAggregatePrepareResult.Prepared>(
            await store.PrepareAggregateAsync(request));
        Assert.IsType<ZLinkAggregatePrepareResult.AlreadyPrepared>(
            await store.PrepareAggregateAsync(
                request with
                {
                    Participants =
                    [
                        participant with
                        {
                            AuthorityPayload =
                                participant.AuthorityPayload.ToArray(),
                            MembershipMutation =
                                participant.MembershipMutation.ToArray()
                        }
                    ],
                    InventoryDigest = request.InventoryDigest.ToArray(),
                    TargetReservations = request.TargetReservations.ToArray()
                }));
        Assert.IsType<ZLinkAggregatePrepareResult.Conflict>(
            await store.PrepareAggregateAsync(
                request with
                {
                    Participants =
                    [
                        participant with
                        {
                            AuthorityPayload = new byte[] { 0xff }
                        }
                    ]
                }));

        Assert.Equal(
            ZLinkAggregateCommitResult.Committed,
            await store.CommitAggregateAsync(prepared.Fence));
        Assert.Equal(
            (0L, 0L),
            store.GetPlacementCapacityUsage(
                sourceDescriptor,
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));
        Assert.Equal(
            (0L, 7L),
            store.GetPlacementCapacityUsage(
                targetDescriptor,
                1,
                ZLinkPlacementObjectKind.Actor,
                "Game.Actor"));
    }

    [Fact]
    public void PublicRelocationContractsMatchTargetShape()
    {
        Assert.Contains(
            typeof(IZLinkRelocationStore).GetMethods(),
            static method => method.Name == "PutRelocationAsync");
        Assert.Contains(
            typeof(IZLinkAuthorityStore).GetMethods(),
            static method => method.Name == "PrepareAggregateAsync");
        Assert.Contains(
            typeof(IZLinkFrameworkOptions).GetMethods(),
            static method => method.Name == "AddRelocationStore");
        Assert.Equal(
            typeof(ValueTask<byte[]>),
            typeof(IZLinkActorRelocationAdapter<>)
                .GetMethod("CaptureAsync")!
                .ReturnType);
        Assert.Equal(
            typeof(ValueTask),
            typeof(IZLinkSpotRelocationAdapter<>)
                .GetMethod("RestoreAsync")!
                .ReturnType);
        Assert.Contains(
            typeof(IZLinkMeshObjectServerBuilder).GetMethods(),
            static method => method.Name == "AddSpotFactory"
                             && method.GetParameters().Length == 3);
        Assert.True(
            typeof(IZLinkActorFactory).IsAssignableFrom(
                typeof(IZLinkActorFactory<TestRelocatableActor>)));
    }

    [Fact]
    public void RelocationStoreRegistrationIsSeparateAndSingle()
    {
        var registration = new ZLinkFrameworkRegistration();
        var options = new ZLinkFrameworkOptionsBuilder(registration);
        var relocation = new RecordingRelocationStore();

        options.AddRelocationStore(relocation);

        Assert.Same(relocation, registration.Locations.RelocationStoreInstance);
        Assert.Null(registration.Locations.StoreInstance);
        Assert.Throws<ZLinkConfigurationException>(
            () => options.AddRelocationStore(new RecordingRelocationStore()));
    }

    [Fact]
    public void ObjectServerRegistrationKeepsPlacementPolicyAndAdapterTogether()
    {
        var registration = new ZLinkSpotNodeRegistration
        {
            SpotNodeName = "objects"
        };
        IZLinkMeshObjectServerBuilder builder = new ZLinkMeshNodeBuilder(registration);

        builder.AddSpotFactory<TestRelocatableSpot>(
            "room",
            new ZLinkObjectPlacementOptions
            {
                PlacementProfiles = ["ssd"],
                MaxActiveObjects = 100,
                MaxPendingActivations = 10
            },
            ZLinkRelocationPolicy<TestRelocatableSpot>
                .Snapshot<TestSpotRelocationAdapter>());

        var relocation = registration.SpotRelocations["room"];
        Assert.Equal(typeof(TestRelocatableSpot), relocation.InstanceType);
        Assert.Equal((byte)2, relocation.PolicyKind);
        Assert.Equal(typeof(TestSpotRelocationAdapter), relocation.AdapterType);
        Assert.Equal(["ssd"], relocation.Placement.PlacementProfiles);
    }

    [Fact]
    public void DescriptorConfigurationKeepsHostAndNodePlacementFields()
    {
        var registration = new ZLinkFrameworkRegistration();
        var options = new ZLinkFrameworkOptionsBuilder(registration)
        {
            ApplicationVersion = 42,
            MaintenanceWave = "wave-blue"
        };
        var node = options.AddRouteMesh("objects");
        node.SetPlacementWeight(75)
            .SetObjectCapacity(500, 25);
        node.Objects().Server().AddActorFactory<
            TestRelocatableActor,
            TestRelocatableActorFactory>(
            "Game.Actor",
            new ZLinkObjectPlacementOptions
            {
                PlacementProfiles = ["ssd"],
                MaxActiveObjects = 100,
                MaxPendingActivations = 10
            },
            ZLinkRelocationPolicy<TestRelocatableActor>.Recreate);

        Assert.Equal(42, registration.ApplicationVersion);
        Assert.Equal("wave-blue", registration.MaintenanceWave);
        var configured = registration.SpotNodes["objects"];
        Assert.Equal(ZLinkMeshNodeObjectRole.Server, configured.ObjectRole);
        Assert.Equal(75, configured.PlacementWeight);
        Assert.Equal(500, configured.MaxActiveObjects);
        Assert.Equal(25, configured.MaxPendingActivations);
        Assert.Throws<ZLinkConfigurationException>(
            () => node.Objects().Client());
    }

    [Fact]
    public void Placement_profile_rejects_values_outside_utf8_contract()
    {
        var registration = new ZLinkFrameworkRegistration();
        var options = new ZLinkFrameworkOptionsBuilder(registration);
        var server = options.AddRouteMesh("objects").Objects().Server();

        Assert.Throws<ZLinkConfigurationException>(
            () => server.AddActorFactory<
                TestRelocatableActor,
                TestRelocatableActorFactory>(
                "Game.Actor",
                new ZLinkObjectPlacementOptions
                {
                    PlacementProfiles = [new string('가', 86)]
                },
                ZLinkRelocationPolicy<TestRelocatableActor>.Recreate));
        Assert.Throws<ZLinkConfigurationException>(
            () => server.AddActorFactory<
                TestRelocatableActor,
                TestRelocatableActorFactory>(
                "Game.Actor.Nul",
                new ZLinkObjectPlacementOptions
                {
                    PlacementProfiles = ["premium\0hidden"]
                },
                ZLinkRelocationPolicy<TestRelocatableActor>.Recreate));
    }

    [Fact]
    public void AggregateEnvelopePreservesAcceptedQueueAndLogicalTimers()
    {
        var envelope = CreateEnvelope();

        var restored = ZLinkRelocationEnvelopeCodec.Decode(
            ZLinkRelocationEnvelopeCodec.Encode(envelope));

        Assert.Equal(envelope.AggregateId, restored.AggregateId);
        Assert.Equal(envelope.AggregateGeneration, restored.AggregateGeneration);
        Assert.Equal(2, restored.Participants.Count);
        var spot = restored.Participants[0];
        Assert.Equal(ZLinkPlacementObjectKind.UserSpot, spot.ObjectKind);
        Assert.Equal(
            new ulong[] { 41, 42 },
            spot.AcceptedJobs.Select(static job => job.AcceptedSequence));
        Assert.Equal(
            new byte[] { 4, 1 },
            spot.AcceptedJobs[0].Payload.ToArray());
        Assert.Equal("heartbeat", spot.LogicalTimers[0].TimerId);
        Assert.Equal(5_000, spot.LogicalTimers[0].PeriodMilliseconds);
        Assert.Equal(
            new byte[] { 7, 7 },
            restored.Participants[1].ApplicationState.ToArray());
    }

    [Fact]
    public void SpotAcceptedJournalPreservesRouteIdentityMetadataAndParts()
    {
        using var received = new ZLinkBackendRouteReceived(
            [
                new Message((ReadOnlySpan<byte>)new byte[] { 1, 2 }),
                new Message((ReadOnlySpan<byte>)new byte[] { 3 })
            ],
            RoutingId.From("source-node"),
            RoutingId.From("spot-7"),
            44,
            reply: null,
            metadata: new ZLinkMessageMetadata(
                new Dictionary<string, string>(StringComparer.Ordinal)
                {
                    ["trace"] = "abc"
                }));

        var restored = ZLinkSpotAcceptedJournal.Decode(
            ZLinkSpotAcceptedJournal.Encode(received));

        Assert.Equal(RoutingId.From("source-node"), restored.SourceNodeRid);
        Assert.Equal(RoutingId.From("spot-7"), restored.SpotRid);
        Assert.Equal<ulong?>(44, restored.RequestSequence);
        Assert.Equal("abc", restored.Metadata.Find("trace"));
        Assert.Equal(new byte[] { 1, 2 }, restored.Parts[0].ToArray());
        Assert.Equal(new byte[] { 3 }, restored.Parts[1].ToArray());
    }

    [Fact]
    public async Task ImmutableRootIsVerifiedBeforeAuthorityCasAndRecoverableAfterPublish()
    {
        var relocation = new RecordingRelocationStore();
        var authority = new RecordingAuthorityStore();
        var coordinator = new ZLinkRelocationPublicationCoordinator(
            authority,
            relocation);
        var request = CreateRequest(CreateEnvelope());

        var published = await coordinator.PublishAsync(request);
        var recovered = await coordinator.RecoverAsync(request.AuthorityKey);

        Assert.NotNull(recovered);
        Assert.Equal(
            new[] { "put", "get", "cas", "read", "get" },
            relocation.Events.Concat(authority.Events)
                .OrderBy(static item => item.Sequence)
                .Select(static item => item.Name));
        Assert.Equal("target-owner", published.Authority.OwnerId);
        Assert.Equal(9, published.Authority.OwnerLeaseGeneration);
        Assert.Equal(1UL, published.Authority.ObjectGeneration);
        Assert.Equal(1UL, published.Authority.AuthorityOwnerGeneration);
        Assert.Equal(
            request.Envelope.InventoryDigest.ToArray(),
            recovered!.Envelope.InventoryDigest.ToArray());
    }

    [Fact]
    public async Task CasConflictDeletesUnpublishedRoot()
    {
        var relocation = new RecordingRelocationStore();
        var authority = new RecordingAuthorityStore { Conflict = true };
        var coordinator = new ZLinkRelocationPublicationCoordinator(
            authority,
            relocation);

        await Assert.ThrowsAsync<ZLinkRelocationPublicationConflictException>(
            async () => await coordinator.PublishAsync(CreateRequest(CreateEnvelope())));

        Assert.Empty(relocation.Payloads);
        Assert.Contains(
            relocation.Events,
            static item => item.Name == "delete");
    }

    [Fact]
    public async Task ExceptionAfterCommittedCasReconcilesWithoutDeletingPublishedRoot()
    {
        var relocation = new RecordingRelocationStore();
        var authority = new RecordingAuthorityStore { ThrowAfterCommit = true };
        var coordinator = new ZLinkRelocationPublicationCoordinator(
            authority,
            relocation);

        var published = await coordinator.PublishAsync(
            CreateRequest(CreateEnvelope()));

        Assert.NotNull(published.Authority);
        Assert.Single(relocation.Payloads);
        Assert.DoesNotContain(
            relocation.Events,
            static item => item.Name == "delete");
    }

    [Fact]
    public async Task MissingPublishedRootIsNonRetriableDataLoss()
    {
        var relocation = new RecordingRelocationStore();
        var authority = new RecordingAuthorityStore();
        var coordinator = new ZLinkRelocationPublicationCoordinator(
            authority,
            relocation);
        var request = CreateRequest(CreateEnvelope());
        var published = await coordinator.PublishAsync(request);
        relocation.Payloads.Remove(published.Relocation.Reference);

        await Assert.ThrowsAsync<ZLinkRelocationDataLostException>(
            async () => await coordinator.RecoverAsync(request.AuthorityKey));
    }

    [Fact]
    public async Task AggregateRelocationPublishesWholeSpotParticipantsWithOneCommit()
    {
        var relocation = new RecordingRelocationStore();
        var authority = new RecordingAuthorityStore();
        var coordinator = new ZLinkAggregateRelocationCoordinator(
            authority,
            relocation);
        var envelope = CreateEnvelope();
        var request = new ZLinkAggregateRelocationRequest(
            envelope.AggregateId,
            envelope.AggregateGeneration,
            envelope.Participants.Select(
                    participant => new ZLinkAggregateRelocationParticipant(
                        participant,
                        $"v-{participant.AuthorityKey.Value}",
                        ZLinkAuthorityGenerationTransition.NewOwner,
                        new byte[] { 6 },
                        new byte[] { 7 }))
                .ToArray(),
            [],
            new ZLinkLocationOwnerToken("aggregate-target", 17));

        var published = await coordinator.PublishAsync(request);

        Assert.Equal(envelope.AggregateId, published.Fence.AggregateId);
        Assert.Equal(2, authority.PublishedCount);
        Assert.Equal(
            new[] { "put", "get", "prepare", "commit" },
            relocation.Events.Concat(authority.Events)
                .OrderBy(static item => item.Sequence)
                .Select(static item => item.Name));
        Assert.Equal(
            ZLinkAggregateInventoryDigest.Compute(request.Participants),
            published.Envelope.InventoryDigest.ToArray());
    }

    [Fact]
    public async Task AggregatePrepareConflictDeletesUnpublishedRoot()
    {
        var relocation = new RecordingRelocationStore();
        var authority = new RecordingAuthorityStore
        {
            AggregatePrepareResult = new ZLinkAggregatePrepareResult.Conflict()
        };
        var coordinator = new ZLinkAggregateRelocationCoordinator(
            authority,
            relocation);
        var envelope = CreateEnvelope();
        var request = new ZLinkAggregateRelocationRequest(
            envelope.AggregateId,
            envelope.AggregateGeneration,
            envelope.Participants.Select(
                    participant => new ZLinkAggregateRelocationParticipant(
                        participant,
                        "v1",
                        ZLinkAuthorityGenerationTransition.NewOwner,
                        ReadOnlyMemory<byte>.Empty,
                        ReadOnlyMemory<byte>.Empty))
                .ToArray(),
            [],
            new ZLinkLocationOwnerToken("aggregate-target", 17));

        await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await coordinator.PublishAsync(request));

        Assert.Empty(relocation.Payloads);
    }

    [Fact]
    public async Task RemoteStatefulDispatchRequiresObservedOwnerGeneration()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "authority-source");
        await using var target = NewNode(context, "authority-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://authority-source-{suffix}";
        var targetEndpoint = $"inproc://authority-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        source.Start();
        target.Start();
        await WaitUntilAsync(
            () => source.Status().AdmittedPeerCount == 1
                  && target.Status().AdmittedPeerCount == 1);

        var actor = target.CreateActor("authority-actor");
        DrainAndDispose(target);
        using var payload = Message.From(new byte[] { 9 });
        Assert.Equal(SubmitResult.NotFound, source.SendToActor(actor, [payload]));

        Assert.True(target.TryGetActorAuthority(actor, out var ownerGeneration));
        source.ObserveActorAuthority(actor, ownerGeneration + 1);
        Assert.Equal(SubmitResult.Ok, source.SendToActor(actor, [payload]));
        await Task.Delay(50);
        using (var ready = new MeshReadyBatch())
        {
            target.DrainReady(
                MeshReadyDomains.Application,
                ready,
                RecvFlags.DontWait);
            Assert.Equal(0, ready.Count);
        }

        source.ObserveActorAuthority(actor, ownerGeneration);
        Assert.Equal(SubmitResult.Ok, source.SendToActor(actor, [payload]));
        await WaitUntilAsync(() =>
        {
            using var ready = new MeshReadyBatch();
            target.DrainReady(
                MeshReadyDomains.Application,
                ready,
                RecvFlags.DontWait);
            return ready.Count == 1;
        });
    }

    private static ZLinkRelocationEnvelope CreateEnvelope()
    {
        var digest = Enumerable.Range(0, 32).Select(static value => (byte)value).ToArray();
        return new ZLinkRelocationEnvelope(
            Guid.Parse("9f952e1b-df66-42bd-84ee-47d48962937a"),
            3,
            digest,
            [
                new ZLinkRelocationParticipantEnvelope(
                    new ZLinkAuthorityKey("spot:mesh:room"),
                    ZLinkPlacementObjectKind.UserSpot,
                    5,
                    11,
                    new byte[] { 1, 2, 3 },
                    [
                        new ZLinkRelocationQueuedJob(41, new byte[] { 4, 1 }),
                        new ZLinkRelocationQueuedJob(42, new byte[] { 4, 2 })
                    ],
                    [
                        new ZLinkRelocationLogicalTimer(
                            "heartbeat",
                            1_900_000_000_000,
                            5_000,
                            new byte[] { 5 })
                    ]),
                new ZLinkRelocationParticipantEnvelope(
                    new ZLinkAuthorityKey("actor:mesh:user-7"),
                    ZLinkPlacementObjectKind.Actor,
                    8,
                    13,
                    new byte[] { 7, 7 },
                    [],
                    [])
            ]);
    }

    private static ZLinkRelocationPublicationRequest CreateRequest(
        ZLinkRelocationEnvelope envelope) =>
        new(
            new ZLinkAuthorityKey("spot:mesh:room"),
            "v0",
            ZLinkAuthorityGenerationTransition.Preserve,
            "target-owner",
            9,
            new byte[] { 8, 8 },
            null,
            envelope);

    private static ZLinkPlacementAllocation TestAllocation() =>
        new(
            ZLinkPlacementAllocationState.Active,
            ZLinkPlacementObjectKind.Actor,
            "Test.Actor",
            new ZLinkMeshNodeDescriptorKey(
                "mesh",
                RoutingId.From("target")),
            1,
            1);

    private static ZLinkObjectReservationRequest ObjectReservation(
        string authorityKey,
        ZLinkMeshNodeDescriptorKey descriptor,
        ZLinkLocationOwnerToken owner,
        int capacityDelta = 1,
        string? placementProfile = null) =>
        new(
            ZLinkPlacementObjectKind.Actor,
            new ZLinkAuthorityKey(authorityKey),
            "Game.Actor",
            placementProfile,
            null,
            $"intent:{authorityKey}",
            SHA256.HashData(
                System.Text.Encoding.UTF8.GetBytes(authorityKey)),
            System.Text.Encoding.UTF8.GetByteCount(authorityKey),
            descriptor,
            1,
            owner,
            new byte[] { 0x10 },
            capacityDelta);

    private static ZLinkMeshNodeDescriptor AuthorityDescriptor(
        string rid,
        ZLinkLocationOwnerToken owner,
        string stableType = "Game.Actor",
        int? activeLimit = null,
        int? pendingLimit = null,
        IReadOnlySet<string>? placementProfiles = null) =>
        new(
            "mesh",
            RoutingId.From(rid),
            LifecycleGeneration: 1,
            DescriptorRevision: 1,
            $"inproc://{rid}",
            new Dictionary<string, int>(StringComparer.Ordinal)
            {
                ["mesh"] = 100
            },
            SecurityIdentity: string.Empty,
            OwnerId: owner.OwnerId,
            LeaseGeneration: owner.LeaseGeneration,
            UpdatedAt: DateTimeOffset.UtcNow)
        {
            ObjectRole = ZLinkMeshNodeObjectRole.Server,
            ObjectCapabilities =
            [
                new ZLinkObjectCapability(
                    ZLinkPlacementObjectKind.Actor,
                    stableType,
                    ZLinkObjectMaintenancePolicyKind.Recreate,
                    HasSnapshotAdapter: false,
                    placementProfiles
                    ?? new HashSet<string>(StringComparer.Ordinal),
                    activeLimit,
                    pendingLimit)
            ],
            State = ZLinkFrameworkRuntimeState.Serving,
            Capacity = new(
                0,
                0,
                activeLimit ?? 10_000,
                pendingLimit ?? 128)
        };

    private static ZLinkRelocationCapacityReservationRequest
        RelocationReservation(
            ZLinkAuthoritySnapshot source,
            ZLinkMeshNodeDescriptorKey sourceDescriptor,
            ZLinkLocationOwnerToken sourceOwner,
            ZLinkMeshNodeDescriptorKey targetDescriptor,
            ZLinkLocationOwnerToken targetOwner) =>
        new(
            Guid.NewGuid(),
            new ZLinkAuthorityKey("actor:mesh:relocate"),
            source.StoreVersion,
            source.Allocation.ObjectKind,
            source.Allocation.StableType,
            sourceDescriptor,
            source.Allocation.DescriptorLifecycleGeneration,
            sourceOwner,
            targetDescriptor,
            1,
            targetOwner,
            source.Allocation.CapacityDelta);

    private static ZLinkManagedMeshNode NewNode(
        IContext context,
        string name)
    {
        var node = new ZLinkManagedMeshNode(context, "mesh");
        node.SetRoutingId(RoutingId.From(name));
        node.AddChannel("mesh");
        return node;
    }

    private static void DrainAndDispose(ZLinkManagedMeshNode node)
    {
        using var ready = new MeshReadyBatch();
        node.DrainReady(MeshReadyDomains.All, ready, RecvFlags.DontWait);
        for (var index = 0; index < ready.Count; index++)
        {
            using var claim = ready.TakeClaim(index);
            using var received = new MeshReceiveBatch();
            while (claim.Receive(received, RecvFlags.DontWait))
                received.Reset();
        }
    }

    private static async Task WaitUntilAsync(Func<bool> predicate)
    {
        var deadline = Stopwatch.GetTimestamp()
                       + (long)(Stopwatch.Frequency * 5);
        while (!predicate())
        {
            if (Stopwatch.GetTimestamp() >= deadline)
                throw new TimeoutException();
            await Task.Delay(10);
        }
    }

    private sealed class RecordingRelocationStore : IZLinkRelocationStore
    {
        internal Dictionary<string, byte[]> Payloads { get; } =
            new(StringComparer.Ordinal);

        internal List<(long Sequence, string Name)> Events { get; } = [];

        public ValueTask<ZLinkRelocationStored> PutRelocationAsync(
            ReadOnlyMemory<byte> payload,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var bytes = payload.ToArray();
            var reference = Convert.ToHexString(
                System.Security.Cryptography.SHA256.HashData(bytes));
            Payloads[reference] = bytes;
            Events.Add((EventClock.Next(), "put"));
            var now = DateTimeOffset.UtcNow;
            return ValueTask.FromResult(new ZLinkRelocationStored(
                reference,
                ZLinkCrc32C.Compute(bytes),
                now + retention,
                now));
        }

        public ValueTask<ZLinkRelocationReadResult> GetRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            Events.Add((EventClock.Next(), "get"));
            return ValueTask.FromResult<ZLinkRelocationReadResult>(
                Payloads.TryGetValue(reference, out var payload)
                    ? new ZLinkRelocationReadResult.Found(payload)
                    : new ZLinkRelocationReadResult.Missing());
        }

        public ValueTask<ZLinkRelocationRenewResult> RenewRelocationAsync(
            string reference,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            var now = DateTimeOffset.UtcNow;
            return ValueTask.FromResult<ZLinkRelocationRenewResult>(
                Payloads.ContainsKey(reference)
                    ? new ZLinkRelocationRenewResult.Renewed(now + retention, now)
                    : new ZLinkRelocationRenewResult.Missing());
        }

        public ValueTask<ZLinkRelocationDeleteResult> DeleteRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default)
        {
            Events.Add((EventClock.Next(), "delete"));
            return ValueTask.FromResult(
                Payloads.Remove(reference)
                    ? ZLinkRelocationDeleteResult.Deleted
                    : ZLinkRelocationDeleteResult.Missing);
        }
    }

    private sealed class RecordingAuthorityStore : IZLinkAuthorityStore
    {
        private readonly Dictionary<string, ZLinkAuthoritySnapshot> _snapshots =
            new(StringComparer.Ordinal);
        private ZLinkAggregatePrepareRequest? _prepared;

        internal bool Conflict { get; init; }

        internal bool ThrowAfterCommit { get; init; }

        internal ZLinkAggregatePrepareResult? AggregatePrepareResult { get; init; }

        internal int PublishedCount => _snapshots.Count;

        internal List<(long Sequence, string Name)> Events { get; } = [];

        public ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
            ZLinkAuthorityKey key,
            CancellationToken cancellationToken = default)
        {
            Events.Add((EventClock.Next(), "read"));
            return ValueTask.FromResult<ZLinkAuthorityReadResult>(
                !_snapshots.TryGetValue(key.Value, out var snapshot)
                    ? new ZLinkAuthorityReadResult.Missing(DateTimeOffset.UtcNow)
                    : new ZLinkAuthorityReadResult.Found(snapshot));
        }

        public ValueTask<ZLinkAuthorityCompareExchangeResult>
            CompareExchangeAuthorityAsync(
                ZLinkAuthorityKey key,
                string expectedStoreVersion,
                ZLinkAuthorityMutation mutation,
                CancellationToken cancellationToken = default)
        {
            Events.Add((EventClock.Next(), "cas"));
            if (Conflict)
                return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                    new ZLinkAuthorityCompareExchangeResult.Conflict(
                        new ZLinkAuthorityReadResult.Missing(DateTimeOffset.UtcNow)));
            var put = Assert.IsType<ZLinkAuthorityMutation.Put>(mutation);
            var targetOwner = put.TargetOwner
                              ?? new ZLinkLocationOwnerToken(
                                  "target-owner",
                                  9);
            var snapshot = new ZLinkAuthoritySnapshot(
                "v1",
                put.Payload,
                1,
                1,
                targetOwner.OwnerId,
                targetOwner.LeaseGeneration,
                TestAllocation(),
                DateTimeOffset.UtcNow);
            _snapshots[key.Value] = snapshot;
            if (ThrowAfterCommit)
                throw new IOException("commit outcome unknown");
            return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                new ZLinkAuthorityCompareExchangeResult.Stored(snapshot));
        }

        public ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
            string prefix,
            ZLinkAuthorityScanCursor? cursor,
            int limit,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkObjectReserveResult> ReserveAsync(
            ZLinkObjectReservationRequest request,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkObjectCommitResult> CommitAsync(
            ZLinkObjectReservation reservation,
            ReadOnlyMemory<byte> readyPayload,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkObjectAbortResult> AbortAsync(
            ZLinkObjectReservation reservation,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
            ZLinkAggregatePrepareRequest request,
            CancellationToken cancellationToken = default)
        {
            Events.Add((EventClock.Next(), "prepare"));
            if (AggregatePrepareResult is { } configured)
                return ValueTask.FromResult(configured);
            _prepared = request;
            return ValueTask.FromResult<ZLinkAggregatePrepareResult>(
                new ZLinkAggregatePrepareResult.Prepared(
                    new ZLinkAggregateFence(
                        request.AggregateId,
                        request.AggregateGeneration)));
        }

        public ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default)
        {
            Events.Add((EventClock.Next(), "commit"));
            Assert.NotNull(_prepared);
            foreach (var participant in _prepared!.Participants)
            {
                Assert.True(ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    participant.AuthorityPayload.Span,
                    out var publication));
                _snapshots[participant.Key.Value] = new ZLinkAuthoritySnapshot(
                    $"v-{participant.Key.Value}-next",
                    participant.AuthorityPayload,
                    1,
                    1,
                    publication.TargetOwnerId,
                    publication.TargetOwnerLeaseGeneration,
                    TestAllocation(),
                    DateTimeOffset.UtcNow);
            }
            return ValueTask.FromResult(ZLinkAggregateCommitResult.Committed);
        }

        public ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default)
        {
            Events.Add((EventClock.Next(), "abort"));
            _prepared = null;
            return ValueTask.FromResult(ZLinkAggregateAbortResult.Aborted);
        }
    }

    private static class EventClock
    {
        private static long _sequence;

        internal static long Next() => Interlocked.Increment(ref _sequence);
    }

    private sealed class TestRelocatableSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }

    private sealed class TestSpotRelocationAdapter
        : IZLinkSpotRelocationAdapter<TestRelocatableSpot>
    {
        public ValueTask<byte[]> CaptureAsync(
            TestRelocatableSpot spot,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(Array.Empty<byte>());

        public ValueTask RestoreAsync(
            TestRelocatableSpot spot,
            ReadOnlyMemory<byte> payload,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class TestRelocatableActor(
        string actorId,
        IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;
    }

    private sealed class TestRelocatableActorFactory
        : IZLinkActorFactory<TestRelocatableActor>
    {
        public ValueTask<TestRelocatableActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(
                new TestRelocatableActor(actorId, context));
    }
}
