using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class RelocationStartupRecoveryTests
{
    [Fact]
    public async Task ActorAndUserSpotAuthoritiesResumeOneAggregatePerScan()
    {
        var fixture = await RecoveryFixture.CreateAsync();
        var offered = 0;
        var applied = new HashSet<Guid>();
        var recovery = new ZLinkRelocationStartupRecovery(
            fixture.Authority,
            fixture.Relocation);

        async ValueTask Resume(
            ZLinkRelocationRecoveryCandidate candidate,
            CancellationToken cancellationToken)
        {
            await Task.Yield();
            cancellationToken.ThrowIfCancellationRequested();
            offered++;
            applied.Add(candidate.Envelope.AggregateId);
            Assert.Equal(2, candidate.Authorities.Count);
            Assert.Equal(
                candidate.Envelope.AggregateId,
                candidate.Reference.AggregateId);
        }

        await recovery.RecoverAsync(Resume);
        await recovery.RecoverAsync(Resume); // process restart/repeated scan

        Assert.Equal(2, offered);
        Assert.Single(applied);
    }

    [Fact]
    public async Task InstanceSpotAuthorityUsesTheSharedSpotPrefixAndIsRecovered()
    {
        var fixture = await RecoveryFixture.CreateAsync(
            ZLinkPlacementObjectKind.InstanceSpot);
        ZLinkRelocationRecoveryCandidate? recovered = null;

        await new ZLinkRelocationStartupRecovery(
                fixture.Authority,
                fixture.Relocation)
            .RecoverAsync(
                (candidate, _) =>
                {
                    recovered = candidate;
                    return ValueTask.CompletedTask;
                });

        Assert.NotNull(recovered);
        Assert.Contains(
            recovered.Authorities,
            static authority =>
                authority.Snapshot.Allocation.ObjectKind
                == ZLinkPlacementObjectKind.InstanceSpot);
    }

    [Fact]
    public async Task ExactReconciliationReadsOnlyStagedParticipantAuthorities()
    {
        var fixture = await RecoveryFixture.CreateAsync();
        var recovery = new ZLinkRelocationStartupRecovery(
            fixture.Authority,
            fixture.Relocation);

        var attempts = await Task.WhenAll(
            recovery.TryReadExactPublishedAsync(fixture.Envelope)
                .AsTask(),
            recovery.TryReadExactPublishedAsync(fixture.Envelope)
                .AsTask());

        Assert.All(attempts, static candidate => Assert.NotNull(candidate));
        Assert.Equal(
            fixture.Envelope.Participants.Count * 2,
            fixture.Authority.ReadCalls.Count);
        Assert.Equal(0, fixture.Authority.ScanCalls);
    }

    [Fact]
    public async Task ExactReconciliationRejectsPartialPublication()
    {
        var fixture = await RecoveryFixture.CreateAsync();
        var entries = fixture.Authority.Entries
            .Select((entry, index) => index == 0
                ? entry
                : entry with
                {
                    Snapshot = entry.Snapshot with
                    {
                        Payload = new byte[] { 1, 2, 3 }
                    }
                })
            .ToArray();
        var recovery = new ZLinkRelocationStartupRecovery(
            new RecoveryAuthorityStore(entries),
            fixture.Relocation);

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            () => recovery.TryReadExactPublishedAsync(fixture.Envelope)
                .AsTask());

        Assert.Equal(ZLinkFrameworkErrorKind.RelocationDataLost, error.Kind);
        Assert.False(error.IsRetriable);
    }

    [Fact]
    public async Task PublishedMissingRootFailsAsNonRetriableRelocationDataLost()
    {
        var fixture = await RecoveryFixture.CreateAsync();
        fixture.Relocation.Remove(fixture.Reference);
        var recovery = new ZLinkRelocationStartupRecovery(
            fixture.Authority,
            fixture.Relocation);

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            async () => await recovery.RecoverAsync(
                static (_, _) => ValueTask.CompletedTask));

        Assert.Equal(ZLinkFrameworkErrorKind.RelocationDataLost, error.Kind);
        Assert.False(error.IsRetriable);
    }

    [Fact]
    public async Task PreparedRootCanBeReadByManifestBeforeAuthorityPublication()
    {
        var authority = new RecoveryAuthorityStore([]);
        var relocation = new RecoveryRelocationStore();
        var coordinator = new ZLinkRelocationPublicationCoordinator(
            authority,
            relocation);
        var envelope = RecoveryFixture.CreateEnvelope();

        var prepared = await coordinator.PrepareAsync(envelope);
        var restored = await coordinator.ReadPreparedAsync(prepared.Reference);

        Assert.Empty(authority.CompareExchangeCalls);
        Assert.Equal(envelope.AggregateId, restored.AggregateId);
        Assert.Equal(
            envelope.Participants.Select(static item => item.AuthorityKey),
            restored.Participants.Select(static item => item.AuthorityKey));
    }

    [Fact]
    public async Task ExactPublishedConflictReconcilesWithoutDeletingRoot()
    {
        var fixture = await RecoveryFixture.CreateAsync();
        var envelope = RecoveryFixture.CreateEnvelope();
        var coordinator = new ZLinkRelocationPublicationCoordinator(
            fixture.Authority,
            fixture.Relocation);
        var prepared = await coordinator.PrepareAsync(envelope);
        var actor = envelope.Participants[0];

        var published = await coordinator.PublishPreparedAsync(
            new ZLinkRelocationPublicationRequest(
                actor.AuthorityKey,
                $"v-{actor.ObjectGeneration}",
                ZLinkAuthorityGenerationTransition.Preserve,
                "target-owner",
                7,
                new byte[] { 9 },
                null,
                envelope),
            prepared);

        Assert.Equal(fixture.Reference, published.Relocation.Reference);
        Assert.True(fixture.Relocation.Contains(fixture.Reference));
        Assert.Single(fixture.Authority.CompareExchangeCalls);
    }

    private sealed record RecoveryFixture(
        RecoveryAuthorityStore Authority,
        RecoveryRelocationStore Relocation,
        string Reference,
        ZLinkRelocationEnvelope Envelope)
    {
        internal static async ValueTask<RecoveryFixture> CreateAsync(
            ZLinkPlacementObjectKind spotKind =
                ZLinkPlacementObjectKind.UserSpot)
        {
            var relocation = new RecoveryRelocationStore();
            var envelope = CreateEnvelope(spotKind);
            var coordinator = new ZLinkRelocationPublicationCoordinator(
                new RecoveryAuthorityStore([]),
                relocation);
            var prepared = await coordinator.PrepareAsync(envelope);
            var publication = new ZLinkRelocationAuthorityPayload(
                prepared.Relocation.Reference,
                prepared.Relocation.ChecksumCrc32c,
                envelope.AggregateId,
                envelope.AggregateGeneration,
                envelope.InventoryDigest,
                "target-owner",
                7,
                new byte[] { 9 });
            var payload = ZLinkRelocationAuthorityPayloadCodec.Encode(publication);
            var entries = envelope.Participants.Select(
                    participant => Entry(participant, payload))
                .ToArray();
            return new RecoveryFixture(
                new RecoveryAuthorityStore(entries),
                relocation,
                prepared.Relocation.Reference,
                envelope);
        }

        internal static ZLinkRelocationEnvelope CreateEnvelope(
            ZLinkPlacementObjectKind spotKind =
                ZLinkPlacementObjectKind.UserSpot)
        {
            var actor = new ZLinkAuthorityKey("zla1:a:7:actor-1");
            var spot = new ZLinkAuthorityKey("zla1:s:6:spot-1");
            return new ZLinkRelocationEnvelope(
                Guid.Parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"),
                4,
                Enumerable.Repeat((byte)0x2a, 32).ToArray(),
                [
                    new ZLinkRelocationParticipantEnvelope(
                        actor,
                        ZLinkPlacementObjectKind.Actor,
                        11,
                        3,
                        new byte[] { 1 },
                        [new ZLinkRelocationQueuedJob(1, new byte[] { 2 })],
                        []),
                    new ZLinkRelocationParticipantEnvelope(
                        spot,
                        spotKind,
                        12,
                        5,
                        new byte[] { 3 },
                        [],
                        [])
                ]);
        }

        private static ZLinkAuthorityEntry Entry(
            ZLinkRelocationParticipantEnvelope participant,
            ReadOnlyMemory<byte> payload) =>
            new(
                participant.AuthorityKey,
                new ZLinkAuthoritySnapshot(
                    $"v-{participant.ObjectGeneration}",
                    payload,
                    participant.ObjectGeneration,
                    participant.AuthorityOwnerGeneration + 1,
                    "target-owner",
                    7,
                    new ZLinkPlacementAllocation(
                        ZLinkPlacementAllocationState.Active,
                        participant.ObjectKind,
                        participant.ObjectKind == ZLinkPlacementObjectKind.Actor
                            ? "Game.Actor"
                            : "Game.Room",
                        new ZLinkMeshNodeDescriptorKey(
                            "mesh",
                            RoutingId.From("target")),
                        2,
                        participant.ObjectKind == ZLinkPlacementObjectKind.Actor
                            ? new ZLinkCapacityVector(1, 0, null)
                            : new ZLinkCapacityVector(
                                0,
                                1,
                                new ZLinkSpotTypeCapacityDelta(
                                    participant.ObjectKind,
                                    "Game.Room",
                                    1))),
                    null,
                    DateTimeOffset.UnixEpoch));
    }

    private sealed class RecoveryRelocationStore : IZLinkRelocationStore
    {
        private readonly Dictionary<string, byte[]> _roots =
            new(StringComparer.Ordinal);

        internal void Remove(string reference) => _roots.Remove(reference);
        internal bool Contains(string reference) => _roots.ContainsKey(reference);

        public ValueTask<ZLinkRelocationStored> PutRelocationAsync(
            ReadOnlyMemory<byte> payload,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var bytes = payload.ToArray();
            var reference = Convert.ToHexString(
                System.Security.Cryptography.SHA256.HashData(bytes));
            _roots[reference] = bytes;
            var now = DateTimeOffset.UnixEpoch;
            return ValueTask.FromResult(new ZLinkRelocationStored(
                reference,
                ZLinkCrc32C.Compute(bytes),
                now + retention,
                now));
        }

        public ValueTask<ZLinkRelocationReadResult> GetRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkRelocationReadResult>(
                _roots.TryGetValue(reference, out var root)
                    ? new ZLinkRelocationReadResult.Found(root)
                    : new ZLinkRelocationReadResult.Missing());

        public ValueTask<ZLinkRelocationRenewResult> RenewRelocationAsync(
            string reference,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            var now = DateTimeOffset.UnixEpoch;
            return ValueTask.FromResult<ZLinkRelocationRenewResult>(
                _roots.ContainsKey(reference)
                    ? new ZLinkRelocationRenewResult.Renewed(
                        now + retention,
                        now)
                    : new ZLinkRelocationRenewResult.Missing());
        }

        public ValueTask<ZLinkRelocationDeleteResult> DeleteRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(
                _roots.Remove(reference)
                    ? ZLinkRelocationDeleteResult.Deleted
                    : ZLinkRelocationDeleteResult.Missing);
    }

    private sealed class RecoveryAuthorityStore(
        IReadOnlyList<ZLinkAuthorityEntry> entries) : IZLinkAuthorityStore
    {
        internal List<ZLinkAuthorityKey> CompareExchangeCalls { get; } = [];
        internal List<ZLinkAuthorityKey> ReadCalls { get; } = [];
        internal IReadOnlyList<ZLinkAuthorityEntry> Entries => entries;
        internal int ScanCalls { get; private set; }

        public ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
            ZLinkAuthorityKey key,
            CancellationToken cancellationToken = default)
        {
            ReadCalls.Add(key);
            return ValueTask.FromResult<ZLinkAuthorityReadResult>(
                entries.FirstOrDefault(item => item.Key == key) is { } entry
                    ? new ZLinkAuthorityReadResult.Found(entry.Snapshot)
                    : new ZLinkAuthorityReadResult.Missing(DateTimeOffset.UnixEpoch));
        }

        public ValueTask<ZLinkAuthorityCompareExchangeResult>
            CompareExchangeAuthorityAsync(
                ZLinkAuthorityKey key,
                string expectedStoreVersion,
                ZLinkAuthorityMutation mutation,
                CancellationToken cancellationToken = default)
        {
            CompareExchangeCalls.Add(key);
            var entry = entries.FirstOrDefault(item => item.Key == key);
            return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                entry is null
                    ? new ZLinkAuthorityCompareExchangeResult.Conflict(
                        new ZLinkAuthorityReadResult.Missing(
                            DateTimeOffset.UnixEpoch))
                    : new ZLinkAuthorityCompareExchangeResult.Conflict(
                        new ZLinkAuthorityReadResult.Found(entry.Snapshot)));
        }

        public ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
            string prefix,
            ZLinkAuthorityScanCursor? cursor,
            int limit,
            CancellationToken cancellationToken = default)
        {
            ScanCalls++;
            return ValueTask.FromResult<ZLinkAuthorityScanResult>(
                new ZLinkAuthorityScanResult.Page(
                    new ZLinkAuthorityPage(
                        entries.Where(item => item.Key.Value.StartsWith(
                                prefix,
                                StringComparison.Ordinal))
                            .Take(limit)
                            .ToArray(),
                        null)));
        }

        public ValueTask<ZLinkObjectReserveResult> ReserveAsync(
            ZLinkObjectReservationRequest request,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkObjectCommitResult> CommitAsync(
            ZLinkObjectReservation reservation,
            ReadOnlyMemory<byte> readyPayload,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkObjectCreationCompleteResult> CompleteCreationAsync(
            ZLinkObjectReservation reservation,
            ZLinkObjectCreationCompletion completion,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkCreationTerminalReadResult>
            ReadCreationTerminalAsync(
                ZLinkCreationOperationId operation,
                CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkObjectAbortResult> AbortAsync(
            ZLinkObjectReservation reservation,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkRelocationCapacityReserveResult>
            ReserveRelocationCapacityAsync(
                ZLinkRelocationCapacityReservationRequest request,
                CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkRelocationCapacityAbortResult>
            AbortRelocationCapacityAsync(
                ZLinkRelocationCapacityFence fence,
                CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
            ZLinkAggregatePrepareRequest request,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
    }
}
