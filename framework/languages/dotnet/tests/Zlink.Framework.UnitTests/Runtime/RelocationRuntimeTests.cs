using System.Diagnostics;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Configuration.Builders;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests;

public sealed class RelocationRuntimeTests
{
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
            new ZLinkAuthorityExpectation.Missing(),
            ZLinkAuthorityGenerationTransition.NewObject,
            "target-owner",
            9,
            new byte[] { 8, 8 },
            envelope);

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
                ZLinkAuthorityExpectation expectation,
                ZLinkAuthorityMutation mutation,
                CancellationToken cancellationToken = default)
        {
            Events.Add((EventClock.Next(), "cas"));
            if (Conflict)
                return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                    new ZLinkAuthorityCompareExchangeResult.Conflict(
                        new ZLinkAuthorityReadResult.Missing(DateTimeOffset.UtcNow)));
            var put = Assert.IsType<ZLinkAuthorityMutation.Put>(mutation);
            Assert.True(ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                put.Payload.Span,
                out var publication));
            var snapshot = new ZLinkAuthoritySnapshot(
                "v1",
                put.Payload,
                1,
                1,
                publication.TargetOwnerId,
                publication.TargetOwnerLeaseGeneration,
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
}
