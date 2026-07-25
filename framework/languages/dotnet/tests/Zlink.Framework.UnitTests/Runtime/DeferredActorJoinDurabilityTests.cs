using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class DeferredActorJoinDurabilityTests
{
    [Fact]
    public async Task Prepared_completion_survives_journal_recreation_with_raw_reply()
    {
        var authority = CreateAuthority();
        var relocation = new TestRelocationStore();
        var actor = new ActorRef("actor-1", 7, "play", RoutingId.From("node-target"));
        var operation = new ZLinkActorJoinOperationId(11, 29);

        var first = new ZLinkDeferredActorJoinCompletionJournal(authority, relocation);
        var prepared = await first.PrepareAsync(
            actor.ActorId,
            operation,
            actor,
            "application/octet-stream",
            new byte[] { 0, 1, 2, 255 },
            CancellationToken.None);

        var recovered = await new ZLinkDeferredActorJoinCompletionJournal(
                authority,
                relocation)
            .RecoverAsync(actor.ActorId, CancellationToken.None);

        Assert.NotNull(recovered);
        Assert.Equal(prepared.Reference, recovered.Reference);
        Assert.Equal(operation, recovered.Completion.OperationId);
        Assert.Equal(actor, recovered.Completion.Actor);
        Assert.Equal(new byte[] { 0, 1, 2, 255 }, recovered.Completion.Reply.ToArray());
        Assert.Equal(
            ZLinkDeferredJoinCompletionCursor.Prepared,
            recovered.Completion.Cursor);
        Assert.Equal((ulong)7, authority.Snapshot.ObjectGeneration);
    }

    [Fact]
    public async Task Committed_cursor_retries_callback_without_repeating_owner_commit()
    {
        var authority = CreateAuthority();
        var relocation = new TestRelocationStore();
        var actor = new ActorRef("actor-1", 7, "play", RoutingId.From("node-target"));
        var operation = new ZLinkActorJoinOperationId(13, 31);
        var journal = new ZLinkDeferredActorJoinCompletionJournal(authority, relocation);
        var root = await journal.PrepareAsync(
            actor.ActorId,
            operation,
            actor,
            null,
            ReadOnlyMemory<byte>.Empty,
            CancellationToken.None);
        root = await journal.MarkCommittedAsync(root, CancellationToken.None);
        var ownerCommitCount = authority.CompareExchangeCount;

        // Simulate process termination after the owner/membership commit but
        // before OnJoinCompletedAsync succeeds.
        var recovered = await new ZLinkDeferredActorJoinCompletionJournal(
                authority,
                relocation)
            .RecoverAsync(actor.ActorId, CancellationToken.None);

        Assert.NotNull(recovered);
        Assert.Equal(
            ZLinkDeferredJoinCompletionCursor.Committed,
            recovered.Completion.Cursor);
        Assert.Equal(ownerCommitCount, authority.CompareExchangeCount);
        Assert.Equal(operation, recovered.Completion.OperationId);
    }

    [Fact]
    public async Task Delivered_cursor_deduplicates_retry_and_releases_root_in_order()
    {
        var authority = CreateAuthority();
        var relocation = new TestRelocationStore();
        var actor = new ActorRef("actor-1", 7, "play", RoutingId.From("node-target"));
        var operation = new ZLinkActorJoinOperationId(17, 37);
        var journal = new ZLinkDeferredActorJoinCompletionJournal(authority, relocation);
        var root = await journal.PrepareAsync(
            actor.ActorId,
            operation,
            actor,
            "raw",
            new byte[] { 9, 8, 7 },
            CancellationToken.None);

        // Retrying Prepare with the same OperationId reuses the published
        // manifest instead of creating another callback record.
        var duplicate = await journal.PrepareAsync(
            actor.ActorId,
            operation,
            actor,
            "raw",
            new byte[] { 9, 8, 7 },
            CancellationToken.None);
        Assert.Equal(root.Reference, duplicate.Reference);

        root = await journal.MarkCommittedAsync(root, CancellationToken.None);
        root = await journal.MarkDeliveredAsync(root, CancellationToken.None);
        var recovered = await journal.RecoverAsync(actor.ActorId, CancellationToken.None);
        Assert.Equal(
            ZLinkDeferredJoinCompletionCursor.Delivered,
            recovered!.Completion.Cursor);

        var referenced = root.Reference;
        await journal.ReleaseAsync(root, CancellationToken.None);

        Assert.Null(await journal.RecoverAsync(actor.ActorId, CancellationToken.None));
        Assert.DoesNotContain(referenced, relocation.Payloads.Keys);
        Assert.True(ZLinkActorAuthorityPayloadCodec.TryDecodeDirect(
            authority.Snapshot.Payload.Span,
            out var actorAuthority));
        Assert.Equal(actor.ActorId, actorAuthority.ActorId);
        Assert.Equal((ulong)7, authority.Snapshot.ObjectGeneration);
    }

    [Fact]
    public async Task Target_completion_waits_for_the_actor_mailbox_turn()
    {
        var state = new ZLinkActorRuntimeState("actor-1");
        state.BindNativeActorRef(
            new ZLinkBackendActorRef(
                RoutingId.From("node-target"),
                "actor-1",
                7));
        state.BindActorInstance(new TestActor("actor-1"));
        var entered = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var order = new List<string>();

        var current = state.ExecuteLifecycleAsync(
            async _ =>
            {
                order.Add("message");
                entered.SetResult();
                await release.Task;
            },
            CancellationToken.None).AsTask();
        await entered.Task;
        var completion = state.ExecuteRelocationCompletionAsync(
            7,
            _ =>
            {
                order.Add("completion");
                return ValueTask.CompletedTask;
            },
            CancellationToken.None).AsTask();

        Assert.False(completion.IsCompleted);
        release.SetResult();
        await Task.WhenAll(current, completion);
        Assert.Equal(["message", "completion"], order);
    }

    private static TestAuthorityStore CreateAuthority()
    {
        var nodeRid = RoutingId.From("node-source");
        var payload = ZLinkActorAuthorityPayloadCodec.Encode(
            new ZLinkActorAuthorityPayload(
                ZLinkActorAuthorityState.Ready,
                "avatar",
                "actor-1",
                "entry-source",
                1,
                ZLinkSpotKind.Entry,
                "owner-source",
                3,
                "mesh",
                nodeRid,
                5));
        return new TestAuthorityStore(
            new ZLinkAuthoritySnapshot(
                "1",
                payload,
                7,
                3,
                "owner-source",
                3,
                new ZLinkPlacementAllocation(
                    ZLinkPlacementAllocationState.Active,
                    ZLinkPlacementObjectKind.Actor,
                    "avatar",
                    new ZLinkMeshNodeDescriptorKey("mesh", nodeRid),
                    5,
                    new ZLinkCapacityVector(1, 0, null)),
                null,
                DateTimeOffset.UtcNow));
    }

    private sealed class TestAuthorityStore(ZLinkAuthoritySnapshot snapshot)
        : IZLinkAuthorityStore
    {
        internal ZLinkAuthoritySnapshot Snapshot { get; private set; } = snapshot;
        internal int CompareExchangeCount { get; private set; }

        public ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
            ZLinkAuthorityKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkAuthorityReadResult>(
                new ZLinkAuthorityReadResult.Found(Snapshot));

        public ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
            string prefix,
            ZLinkAuthorityScanCursor? cursor,
            int limit,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkAuthorityCompareExchangeResult> CompareExchangeAuthorityAsync(
            ZLinkAuthorityKey key,
            string expectedStoreVersion,
            ZLinkAuthorityMutation mutation,
            CancellationToken cancellationToken = default)
        {
            CompareExchangeCount++;
            if (!string.Equals(
                    expectedStoreVersion,
                    Snapshot.StoreVersion,
                    StringComparison.Ordinal))
                return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                    new ZLinkAuthorityCompareExchangeResult.Conflict(
                        new ZLinkAuthorityReadResult.Found(Snapshot)));
            if (mutation is not ZLinkAuthorityMutation.Put put)
                throw new NotSupportedException();
            Snapshot = Snapshot with
            {
                StoreVersion = (int.Parse(Snapshot.StoreVersion) + 1).ToString(),
                Payload = put.Payload,
                StoreNow = DateTimeOffset.UtcNow
            };
            return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                new ZLinkAuthorityCompareExchangeResult.Stored(Snapshot));
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

        public ValueTask<ZLinkCreationTerminalReadResult> ReadCreationTerminalAsync(
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

    private sealed class TestRelocationStore : IZLinkRelocationStore
    {
        internal Dictionary<string, byte[]> Payloads { get; } = [];

        public ValueTask<ZLinkRelocationStored> PutRelocationAsync(
            ReadOnlyMemory<byte> payload,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            var reference = Guid.NewGuid().ToString("n");
            Payloads.Add(reference, payload.ToArray());
            var now = DateTimeOffset.UtcNow;
            return ValueTask.FromResult(
                new ZLinkRelocationStored(
                    reference,
                    ZLinkCrc32C.Compute(payload.Span),
                    now + retention,
                    now));
        }

        public ValueTask<ZLinkRelocationReadResult> GetRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkRelocationReadResult>(
                Payloads.TryGetValue(reference, out var payload)
                    ? new ZLinkRelocationReadResult.Found(payload)
                    : new ZLinkRelocationReadResult.Missing());

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
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(
                Payloads.Remove(reference)
                    ? ZLinkRelocationDeleteResult.Deleted
                    : ZLinkRelocationDeleteResult.Missing);
    }

    private sealed class TestActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;
        public IZLinkActorContext Context { get; } = new TestActorContext(actorId);
    }
}
