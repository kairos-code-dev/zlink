using System.Diagnostics;
using System.Security.Cryptography;
using Systems.Zlink.Framework.Runtime.Protocol;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Service;

namespace Zlink.Framework.UnitTests;

public sealed class CanonicalRelocationReservationOwnerTests
{
    [Fact]
    public async Task Identical_command40_retries_join_one_side_effect_free_offer()
    {
        await using var fixture = await Fixture.CreateAsync(requiredMessages: 0,
            requiredBytes: 0);

        var offers = await Task.WhenAll(
            fixture.Owner.OfferAsync(fixture.Prepare, fixture.SourceRid,
                CancellationToken.None).AsTask(),
            fixture.Owner.OfferAsync(fixture.Prepare, fixture.SourceRid,
                CancellationToken.None).AsTask());

        Assert.Equal(offers[0], offers[1]);
        Assert.Equal((byte)2, offers[0].Role);
        Assert.Empty(offers[0].Participants);
        Assert.True(offers[0].OfferedMessages > 0);
        Assert.True(offers[0].OfferedBytes > 0);
        Assert.Equal(0, fixture.Permits.Snapshot().InboundUnits);
        var current = Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await fixture.Store.ReadAuthorityAsync(fixture.AuthorityKey));
        Assert.Equal(fixture.SourceRid, current.Snapshot.Allocation.Descriptor.Rid);
    }

    [Fact]
    public async Task Retry_mismatch_role_and_inventory_are_rejected()
    {
        await using var fixture = await Fixture.CreateAsync();
        var offer = await fixture.Owner.OfferAsync(fixture.Prepare,
            fixture.SourceRid, CancellationToken.None);

        await Assert.ThrowsAsync<InvalidDataException>(() => fixture.Owner
            .OfferAsync(fixture.Prepare with { RequiredBytes = 65 },
                fixture.SourceRid, CancellationToken.None).AsTask());
        await Assert.ThrowsAsync<InvalidDataException>(() => fixture.Owner
            .AcceptAsync(offer, fixture.SourceRid, CancellationToken.None)
            .AsTask());
        var acceptance = fixture.Acceptance(offer);
        await Assert.ThrowsAsync<InvalidDataException>(() => fixture.Owner
            .AcceptAsync(acceptance with
                {
                    Participants =
                    [acceptance.Participants[0] with { AllowanceBytes = 65 }]
                }, fixture.SourceRid, CancellationToken.None).AsTask());
        await Assert.ThrowsAsync<InvalidDataException>(() => fixture.Owner
            .AcceptAsync(acceptance, RoutingId.From("wrong-source"),
                CancellationToken.None).AsTask());
    }

    [Fact]
    public async Task Deadline_cleanup_and_accept_to_stage_transition_are_fenced()
    {
        await using var expired = await Fixture.CreateAsync();
        var expiredOffer = await expired.Owner.OfferAsync(expired.Prepare,
            expired.SourceRid, CancellationToken.None);
        expired.Time.Advance(TimeSpan.FromSeconds(6));
        Assert.Equal(1, expired.Owner.ExpireOffers());
        await Assert.ThrowsAsync<InvalidDataException>(() => expired.Owner
            .AcceptAsync(expired.Acceptance(expiredOffer), expired.SourceRid,
                CancellationToken.None).AsTask());
        Assert.Equal(0, expired.Permits.Snapshot().InboundUnits);

        await using var active = await Fixture.CreateAsync();
        await active.Owner.OfferAsync(active.Prepare, active.SourceRid,
            CancellationToken.None);
        Assert.Throws<InvalidDataException>(() => active.Owner.BeginStaging(
            active.Prepare.RelocationId,
            active.Prepare.TargetAttemptGeneration));
        var offer = await active.Owner.OfferAsync(active.Prepare,
            active.SourceRid, CancellationToken.None);
        var acceptance = active.Acceptance(offer);
        var reserved = await active.Owner.AcceptAsync(acceptance,
            active.SourceRid, CancellationToken.None);
        Assert.Equal(acceptance.Participants, reserved.Participants);
        Assert.Equal(1, active.Permits.Snapshot().InboundUnits);
        active.Owner.BeginStaging(active.Prepare.RelocationId,
            active.Prepare.TargetAttemptGeneration);
        active.Owner.BeginStaging(active.Prepare.RelocationId,
            active.Prepare.TargetAttemptGeneration);
        var retry = await active.Owner.AcceptAsync(acceptance,
            active.SourceRid, CancellationToken.None);
        Assert.Equal(reserved, retry);
    }

    [Fact]
    public async Task Identical_accept_retry_joins_in_flight_store_reservation()
    {
        await using var fixture = await Fixture.CreateAsync();
        var blocking = new BlockingAuthorityStore(fixture.Store);
        var owner = new ZLinkCanonicalRelocationReservationOwner(
            blocking, fixture.Permits, "mesh", RoutingId.From("reservation-target"),
            1, TimeSpan.FromSeconds(5), fixture.Time);
        await using var ownerLifetime = owner;
        var offer = await owner.OfferAsync(fixture.Prepare, fixture.SourceRid,
            CancellationToken.None);
        var acceptance = fixture.Acceptance(offer);

        var first = owner.AcceptAsync(acceptance, fixture.SourceRid,
            CancellationToken.None).AsTask();
        await blocking.Entered.Task.WaitAsync(TimeSpan.FromSeconds(1));
        var retry = owner.AcceptAsync(acceptance, fixture.SourceRid,
            CancellationToken.None).AsTask();
        blocking.Release.TrySetResult();

        Assert.Equal(await first, await retry);
        Assert.Equal(1, blocking.ReserveCalls);
    }

    [Fact]
    public async Task Accepting_deadline_releases_runtime_permit_and_allows_retry()
    {
        await using var fixture = await Fixture.CreateAsync();
        var blocking = new BlockingAuthorityStore(fixture.Store);
        await using var owner = new ZLinkCanonicalRelocationReservationOwner(
            blocking, fixture.Permits, "mesh", RoutingId.From("reservation-target"),
            1, TimeSpan.FromMilliseconds(50), fixture.Time);
        var offer = await owner.OfferAsync(fixture.Prepare, fixture.SourceRid,
            CancellationToken.None);

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => owner
            .AcceptAsync(fixture.Acceptance(offer), fixture.SourceRid,
                CancellationToken.None).AsTask());

        Assert.Equal(0, fixture.Permits.Snapshot().InboundUnits);
        blocking.Release.TrySetResult();
        _ = await owner.AcceptAsync(fixture.Acceptance(offer), fixture.SourceRid,
            CancellationToken.None);
        Assert.Equal(1, fixture.Permits.Snapshot().InboundUnits);
    }

    [Fact]
    public async Task Successful_standalone_reservations_reuse_one_permit_64_times()
    {
        var permits = new ZLinkRelocationPermitPool(new ZLinkLocationOptions
        {
            MaxActiveInboundRelocations = 1
        });

        for (var index = 0; index < 64; index++)
        {
            await using var fixture = await Fixture.CreateAsync(permits: permits);
            var offer = await fixture.Owner.OfferAsync(
                fixture.Prepare, fixture.SourceRid, CancellationToken.None);
            _ = await fixture.Owner.AcceptAsync(
                fixture.Acceptance(offer),
                fixture.SourceRid,
                CancellationToken.None);
            fixture.Owner.BeginStaging(
                fixture.Prepare.RelocationId,
                fixture.Prepare.TargetAttemptGeneration);

            Assert.True(fixture.Owner.CompleteSuccessfulStaging(
                fixture.Prepare.RelocationId,
                fixture.Prepare.TargetAttemptGeneration));
            Assert.False(fixture.Owner.CompleteSuccessfulStaging(
                fixture.Prepare.RelocationId,
                fixture.Prepare.TargetAttemptGeneration));
            Assert.Equal(0, permits.Snapshot().InboundUnits);
        }
    }

    [Fact]
    public async Task Reservation_slot_limit_rejects_a_distinct_command40()
    {
        await using var fixture = await Fixture.CreateAsync();
        await using var owner = new ZLinkCanonicalRelocationReservationOwner(
            fixture.Store, fixture.Permits, "mesh",
            RoutingId.From("reservation-target"), 1, TimeSpan.FromSeconds(5),
            fixture.Time, maximumSlots: 1);
        _ = await owner.OfferAsync(fixture.Prepare, fixture.SourceRid,
            CancellationToken.None);

        await Assert.ThrowsAsync<ZLinkFrameworkException>(() => owner.OfferAsync(
            fixture.Prepare with
            {
                RelocationId = new ZLinkServiceWireCodec.RelocationWireId(3, 4)
            }, fixture.SourceRid, CancellationToken.None).AsTask());
    }

    [Fact]
    public async Task Command35_rejects_pending_source_cleanup_state()
    {
        await using var fixture = await Fixture.CreateAsync();
        var complete = new ZLinkServiceWireCodec.RelocationCompleteRecord(
            fixture.Prepare.RelocationId,
            fixture.Prepare.TargetAttemptGeneration,
            fixture.Prepare.Coordinator,
            1,
            new ZLinkServiceWireCodec.RequestSourceFence(
                "source-owner",
                1,
                fixture.SourceRid,
                fixture.Prepare.SourceNodeGeneration),
            0);

        await Assert.ThrowsAsync<InvalidDataException>(() => fixture.Owner
            .CompleteAsync(complete, fixture.SourceRid, CancellationToken.None)
            .AsTask());
    }

    [Fact]
    public async Task Raw_command40_and_30_ingress_returns_exact_command41()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "raw-reservation-source");
        await using var target = NewNode(context, "raw-reservation-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://raw-reservation-source-{suffix}";
        var targetEndpoint = $"inproc://raw-reservation-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        source.Start();
        target.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 1
            && target.Status().AdmittedPeerCount == 1);

        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var sourceLease = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync("raw-source-owner",
                TimeSpan.FromMinutes(1)));
        var targetLease = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync("raw-target-owner",
                TimeSpan.FromMinutes(1)));
        var sourceDescriptor = Fixture.Descriptor(source.RoutingId,
            sourceLease.Token, source.Status().LifecycleGeneration);
        var targetDescriptor = Fixture.Descriptor(target.RoutingId,
            targetLease.Token, target.Status().LifecycleGeneration);
        await store.UpdateMeshNodeAsync(sourceDescriptor,
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateMeshNodeAsync(targetDescriptor,
            ZLinkLocationWriteIntent.NewClaim);
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey("raw-actor");
        var creation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(new ZLinkObjectReservationRequest(
                ZLinkPlacementObjectKind.Actor, key, "Game.Actor", "intent",
                SHA256.HashData(new byte[] { 1 }), 1,
                new ZLinkMeshNodeDescriptorKey("mesh", source.RoutingId),
                source.Status().LifecycleGeneration, sourceLease.Token,
                new byte[] { 1 }, new ZLinkCapacityVector(1, 0, null))));
        var sourceAuthority = new ZLinkActorAuthorityPayload(
            ZLinkActorAuthorityState.Ready,
            "Game.Actor",
            "raw-actor",
            sourceDescriptor.EntrySpotId!,
            source.Status().LifecycleGeneration,
            ZLinkSpotKind.Entry,
            sourceLease.Token.OwnerId,
            checked((ulong)sourceLease.Token.LeaseGeneration),
            "mesh",
            source.RoutingId,
            source.Status().LifecycleGeneration);
        var ready = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                creation.Reservation,
                ZLinkActorAuthorityPayloadCodec.Encode(sourceAuthority)));
        var relocationId = Guid.NewGuid();
        var envelope = ZLinkCanonicalActorRelocationWriter.CreateInitial(
            ZLinkStandaloneActorRelocationRuntime.CreateImmutableRoot(
                ready.Snapshot,
                sourceAuthority,
                targetDescriptor,
                relocationId,
                ReadOnlyMemory<byte>.Empty,
                [],
                default),
            applicationVersion: 1);
        var capturedPayload = ZLinkCanonicalRelocationAuthorityStateCodec
            .ReplaceRelocationState(
                ready.Snapshot.Payload.Span,
                new ZLinkCanonicalRelocationAuthorityState(
                    envelope.CanonicalRelocationHigh,
                    envelope.CanonicalRelocationLow,
                    0,
                    source.RoutingId.ToHex(),
                    source.Status().LifecycleGeneration,
                    sourceLease.Token.OwnerId,
                    checked((ulong)sourceLease.Token.LeaseGeneration),
                    string.Empty,
                    0,
                    string.Empty,
                    0,
                    0,
                    sourceLease.Token.OwnerId,
                    checked((ulong)sourceLease.Token.LeaseGeneration),
                    source.RoutingId.ToHex(),
                    source.Status().LifecycleGeneration,
                    2,
                    "root",
                    3,
                    1,
                    0),
                envelope);
        var captured = Assert.IsType<ZLinkAuthorityCompareExchangeResult.Stored>(
            await store.CompareExchangeAuthorityAsync(
                key,
                ready.Snapshot.StoreVersion,
                new ZLinkAuthorityMutation.Put(
                    capturedPayload,
                    ZLinkAuthorityGenerationTransition.Preserve,
                    null,
                    null)));
        var permits = new ZLinkRelocationPermitPool(new ZLinkLocationOptions());
        await using var reservationOwner =
            new ZLinkCanonicalRelocationReservationOwner(
                store, permits, "mesh", target.RoutingId,
                target.Status().LifecycleGeneration,
                TimeSpan.FromSeconds(5), time);
        target.SetCanonicalRelocationReservationTarget(reservationOwner);
        var participant = new ZLinkServiceWireCodec.RelocationParticipantRecord(
            1, 1, default, 0, null, 0, default, 0, 1, 64);
        var prepare = new ZLinkServiceWireCodec.RelocationPrepareRecord(
            new ZLinkServiceWireCodec.RelocationWireId(
                envelope.CanonicalRelocationHigh,
                envelope.CanonicalRelocationLow), 1, 1,
            new ZLinkServiceWireCodec.RelocationCoordinatorFence(
                sourceLease.Token.OwnerId,
                checked((ulong)sourceLease.Token.LeaseGeneration), source.RoutingId,
                source.Status().LifecycleGeneration,
                captured.Snapshot.StoreVersion),
            new ZLinkServiceWireCodec.RelocationCandidateRecord(
                target.RoutingId, target.Status().LifecycleGeneration,
                targetLease.Token.OwnerId,
                checked((ulong)targetLease.Token.LeaseGeneration)),
            1, new ZLinkServiceWireCodec.RelocationObjectRecord(
                1, string.Empty, "raw-actor", captured.Snapshot.ObjectGeneration,
                captured.Snapshot.AuthorityOwnerGeneration),
            source.RoutingId, source.Status().LifecycleGeneration,
            1, 64, [participant],
            new ZLinkServiceWireCodec.RelocationRootRecord("root", 3), 1);

        var reservations = await Task.WhenAll(
            source.ReserveCanonicalRelocationAsync(
                target.RoutingId, prepare, TimeSpan.FromSeconds(5),
                CancellationToken.None).AsTask(),
            source.ReserveCanonicalRelocationAsync(
                target.RoutingId, prepare, TimeSpan.FromSeconds(5),
                CancellationToken.None).AsTask());
        var reserved = reservations[0];

        Assert.Equal(reservations[0], reservations[1]);
        Assert.Equal(prepare.RelocationId, reserved.RelocationId);
        Assert.Equal(prepare.Participants, reserved.Participants);
        Assert.Equal(1, permits.Snapshot().InboundUnits);
    }

    [Fact]
    public async Task Raw_two_node_command31_32_34_and_35_use_infrastructure_dispatch()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "raw-attempt-source");
        await using var target = NewNode(context, "raw-attempt-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://raw-attempt-source-{suffix}";
        var targetEndpoint = $"inproc://raw-attempt-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        var attempt = new RawAttemptTarget(source.RoutingId,
            source.Status().LifecycleGeneration,
            target.RoutingId,
            target.Status().LifecycleGeneration);
        target.SetCanonicalRelocationReservationTarget(attempt);
        source.Start();
        target.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 1
            && target.Status().AdmittedPeerCount == 1);

        var prepare = attempt.Prepare;
        var frozen = ZLinkServiceWireCodec.EncodeFrozenRelocationControl(
            new ZLinkServiceWireCodec.FrozenRelocationControlRecord(
                new ZLinkServiceWireCodec.RequestSourceFence(
                    "source-owner", 1, source.RoutingId,
                    source.Status().LifecycleGeneration),
                default, 1, 1, prepare.RelocationId, prepare.Object, 0,
                ServiceWireConstants.FrameworkErrorCode.None));
        var data = new ZLinkServiceWireCodec.RelocationDataRecord(
            prepare.RelocationId, prepare.TargetAttemptGeneration,
            prepare.Coordinator, 1, 1, 1, frozen);

        await source.StageCanonicalRelocationAsync(
            target.RoutingId, prepare, [data], TimeSpan.FromSeconds(5),
            CancellationToken.None);
        // Until command 35 closes the source attempt, the exact command 34
        // response is retried so one lost transport submission cannot strand
        // an authorized target commit.
        await WaitUntilAsync(() => attempt.SealResponses >= 2);
        await source.CompleteCanonicalRelocationAsync(
            target.RoutingId,
            new ZLinkServiceWireCodec.RelocationCompleteRecord(
                prepare.RelocationId, prepare.TargetAttemptGeneration,
                prepare.Coordinator, 1,
                new ZLinkServiceWireCodec.RequestSourceFence(
                    "source-owner", 1, source.RoutingId,
                    source.Status().LifecycleGeneration),
                1),
            CancellationToken.None);
        await WaitUntilAsync(() => attempt.Completions == 1);
        var terminalSealResponses = attempt.SealResponses;
        await Task.Delay(250);

        Assert.Equal(1, attempt.DataRecords);
        Assert.True(terminalSealResponses >= 2);
        Assert.Equal(terminalSealResponses, attempt.SealResponses);
        Assert.Equal(1, attempt.Completions);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task Raw_node_teardown_is_bounded_and_rejects_late_inbound_reply(
        bool forceStop)
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "raw-supervisor-source");
        var target = NewNode(
            context,
            "raw-supervisor-target",
            TimeSpan.FromMilliseconds(50));
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://raw-supervisor-source-{suffix}";
        var targetEndpoint = $"inproc://raw-supervisor-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        var blocking = new BlockingOfferTarget(
            source.RoutingId,
            source.Status().LifecycleGeneration,
            target.RoutingId,
            target.Status().LifecycleGeneration);
        target.SetCanonicalRelocationReservationTarget(blocking);
        source.Start();
        target.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 1
            && target.Status().AdmittedPeerCount == 1);
        using var requestCancellation = new CancellationTokenSource();
        var request = source.ReserveCanonicalRelocationAsync(
            target.RoutingId,
            blocking.Prepare,
            TimeSpan.FromSeconds(30),
            requestCancellation.Token).AsTask();
        await blocking.Entered.Task.WaitAsync(TimeSpan.FromSeconds(5));

        using var forceStopBound = new CancellationTokenSource(
            TimeSpan.FromMilliseconds(50));
        var stopwatch = Stopwatch.StartNew();
        var dispose = forceStop
            ? target.ForceStopAsync(forceStopBound.Token).AsTask()
            : target.DisposeAsync().AsTask();
        await blocking.CancellationObserved.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await dispose.WaitAsync(TimeSpan.FromSeconds(5));
        stopwatch.Stop();
        Assert.True(stopwatch.Elapsed < TimeSpan.FromSeconds(1));
        Assert.False(blocking.Exited.Task.IsCompleted);

        blocking.AllowExit.TrySetResult();
        await blocking.Exited.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await Task.Delay(100);
        Assert.Equal(1, blocking.OfferCount);
        Assert.False(request.IsCompleted);

        requestCancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => request);
    }

    [Fact]
    public void Production_source_has_no_private_spot_relocation_wire()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null
               && !Directory.Exists(Path.Combine(directory.FullName,
                   "framework", "languages", "dotnet", "src",
                   "Zlink.Framework")))
            directory = directory.Parent;
        Assert.NotNull(directory);
        var source = Path.Combine(directory!.FullName,
            "framework", "languages", "dotnet", "src");
        var text = string.Join('\n', Directory.EnumerateFiles(
                source, "*.cs", SearchOption.AllDirectories)
            .Select(File.ReadAllText));
        foreach (var symbol in new[]
                 {
                     "zlink.internal.spot.relocation." + "stage.v1",
                     "zlink.internal.spot.relocation." + "publish.v1",
                     "zlink.internal.spot.relocation." + "abort.v1",
                     "zlink.internal.spot.relocation." + "held-relay.v1",
                     "ZLinkSpotRetire" + "StageHandler",
                     "ZLinkSpotRetire" + "PublishHandler",
                     "ZLinkSpotRetire" + "AbortHandler",
                     "ZLinkSpotRetire" + "HeldRelayHandler"
                 })
            Assert.DoesNotContain(symbol, text, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Dispose_releases_accepted_runtime_and_store_reservation()
    {
        var fixture = await Fixture.CreateAsync();
        var offer = await fixture.Owner.OfferAsync(fixture.Prepare,
            fixture.SourceRid, CancellationToken.None);
        var acceptance = fixture.Acceptance(offer);
        _ = await fixture.Owner.AcceptAsync(acceptance, fixture.SourceRid,
            CancellationToken.None);
        Assert.Equal(1, fixture.Permits.Snapshot().InboundUnits);

        await fixture.DisposeAsync();

        Assert.Equal(0, fixture.Permits.Snapshot().InboundUnits);
    }

    [Fact]
    public async Task Abandoned_accept_is_expired_and_releases_its_permit()
    {
        await using var fixture = await Fixture.CreateAsync();
        var offer = await fixture.Owner.OfferAsync(fixture.Prepare,
            fixture.SourceRid, CancellationToken.None);
        _ = await fixture.Owner.AcceptAsync(fixture.Acceptance(offer),
            fixture.SourceRid, CancellationToken.None);
        fixture.Time.Advance(TimeSpan.FromSeconds(6));

        Assert.Equal(1, await fixture.Owner.ExpireAbandonedAsync());
        Assert.Equal(0, fixture.Permits.Snapshot().InboundUnits);
    }

    [Fact]
    public async Task Stale_coordinator_lease_rejects_accept_before_capacity_reservation()
    {
        await using var fixture = await Fixture.CreateAsync();
        var offer = await fixture.Owner.OfferAsync(fixture.Prepare,
            fixture.SourceRid, CancellationToken.None);
        var released = await fixture.Store.ReleaseOwnerLeaseAsync(
            fixture.CoordinatorOwner);
        Assert.Equal(ZLinkOwnerLeaseReleaseResult.Released, released);

        await Assert.ThrowsAsync<InvalidDataException>(() => fixture.Owner
            .AcceptAsync(fixture.Acceptance(offer), fixture.SourceRid,
                CancellationToken.None).AsTask());

        Assert.Equal(0, fixture.Permits.Snapshot().InboundUnits);
        var current = Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await fixture.Store.ReadAuthorityAsync(fixture.AuthorityKey));
        Assert.Equal(fixture.SourceRid, current.Snapshot.Allocation.Descriptor.Rid);
    }

    private sealed record Fixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkRelocationPermitPool Permits,
        ZLinkCanonicalRelocationReservationOwner Owner,
        ManualTimeProvider Time,
        RoutingId SourceRid,
        ZLinkLocationOwnerToken CoordinatorOwner,
        ZLinkAuthorityKey AuthorityKey,
        ZLinkServiceWireCodec.RelocationPrepareRecord Prepare)
        : IAsyncDisposable
    {
        internal static async Task<Fixture> CreateAsync(
            ulong requiredMessages = 1,
            ulong requiredBytes = 64,
            ZLinkRelocationPermitPool? permits = null)
        {
            var time = new ManualTimeProvider();
            var store = new ZLinkInMemoryLocationStore(time);
            var sourceRid = RoutingId.From("reservation-source");
            var targetRid = RoutingId.From("reservation-target");
            var sourceLease = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
                await store.ClaimOwnerLeaseAsync("source-owner",
                    TimeSpan.FromMinutes(1)));
            var targetLease = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
                await store.ClaimOwnerLeaseAsync("target-owner",
                    TimeSpan.FromMinutes(1)));
            var coordinatorLease =
                Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
                    await store.ClaimOwnerLeaseAsync("coordinator-owner",
                        TimeSpan.FromMinutes(1)));
            var coordinatorRid = RoutingId.From("reservation-coordinator");
            var sourceDescriptor = Descriptor(sourceRid, sourceLease.Token);
            var targetDescriptor = Descriptor(targetRid, targetLease.Token);
            await store.UpdateMeshNodeAsync(sourceDescriptor,
                ZLinkLocationWriteIntent.NewClaim);
            await store.UpdateMeshNodeAsync(targetDescriptor,
                ZLinkLocationWriteIntent.NewClaim);
            await store.UpdateMeshNodeAsync(
                Descriptor(coordinatorRid, coordinatorLease.Token),
                ZLinkLocationWriteIntent.NewClaim);
            var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey("actor-1");
            var creation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
                await store.ReserveAsync(new ZLinkObjectReservationRequest(
                    ZLinkPlacementObjectKind.Actor, key, "Game.Actor", "intent",
                    SHA256.HashData([1]), 1,
                    new ZLinkMeshNodeDescriptorKey("mesh", sourceRid), 1,
                    sourceLease.Token, new byte[] { 1 },
                    new ZLinkCapacityVector(1, 0, null))));
            var ready = Assert.IsType<ZLinkObjectCommitResult.Committed>(
                await store.CommitAsync(creation.Reservation,
                    ZLinkActorAuthorityPayloadCodec.Encode(
                        new ZLinkActorAuthorityPayload(
                            ZLinkActorAuthorityState.Ready,
                            "Game.Actor",
                            "actor-1",
                            sourceDescriptor.EntrySpotId!,
                            1,
                            ZLinkSpotKind.Entry,
                            sourceLease.Token.OwnerId,
                            checked((ulong)sourceLease.Token.LeaseGeneration),
                            "mesh",
                            sourceRid,
                            1))));
            var sourceAuthority = new ZLinkActorAuthorityPayload(
                ZLinkActorAuthorityState.Ready,
                "Game.Actor",
                "actor-1",
                sourceDescriptor.EntrySpotId!,
                1,
                ZLinkSpotKind.Entry,
                sourceLease.Token.OwnerId,
                checked((ulong)sourceLease.Token.LeaseGeneration),
                "mesh",
                sourceRid,
                1);
            var relocationId = Guid.NewGuid();
            var envelope = ZLinkCanonicalActorRelocationWriter.CreateInitial(
                ZLinkStandaloneActorRelocationRuntime.CreateImmutableRoot(
                    ready.Snapshot,
                    sourceAuthority,
                    targetDescriptor,
                    relocationId,
                    ReadOnlyMemory<byte>.Empty,
                    [],
                    default),
                applicationVersion: 1);
            var capturedPayload =
                ZLinkCanonicalRelocationAuthorityStateCodec
                    .ReplaceRelocationState(
                        ready.Snapshot.Payload.Span,
                        new ZLinkCanonicalRelocationAuthorityState(
                            envelope.CanonicalRelocationHigh,
                            envelope.CanonicalRelocationLow,
                            0,
                            sourceRid.ToHex(),
                            1,
                            sourceLease.Token.OwnerId,
                            checked((ulong)sourceLease.Token.LeaseGeneration),
                            string.Empty,
                            0,
                            string.Empty,
                            0,
                            0,
                            coordinatorLease.Token.OwnerId,
                            checked((ulong)coordinatorLease.Token.LeaseGeneration),
                            coordinatorRid.ToHex(),
                            1,
                            2,
                            "root",
                            3,
                            1,
                            0),
                        envelope);
            var captured = Assert.IsType<
                ZLinkAuthorityCompareExchangeResult.Stored>(
                await store.CompareExchangeAuthorityAsync(
                    key,
                    ready.Snapshot.StoreVersion,
                    new ZLinkAuthorityMutation.Put(
                        capturedPayload,
                        ZLinkAuthorityGenerationTransition.Preserve,
                        null,
                        null)));
            var participant = new ZLinkServiceWireCodec.RelocationParticipantRecord(
                1, 1, default, 0, null, 0, default, 0,
                requiredMessages, requiredBytes);
            var coordinator = new ZLinkServiceWireCodec.RelocationCoordinatorFence(
                coordinatorLease.Token.OwnerId,
                checked((ulong)coordinatorLease.Token.LeaseGeneration),
                coordinatorRid, 1,
                captured.Snapshot.StoreVersion);
            var candidate = new ZLinkServiceWireCodec.RelocationCandidateRecord(
                targetRid, 1, targetLease.Token.OwnerId,
                checked((ulong)targetLease.Token.LeaseGeneration));
            var prepare = new ZLinkServiceWireCodec.RelocationPrepareRecord(
                new ZLinkServiceWireCodec.RelocationWireId(
                    envelope.CanonicalRelocationHigh,
                    envelope.CanonicalRelocationLow), 1, 1,
                coordinator, candidate, 1,
                new ZLinkServiceWireCodec.RelocationObjectRecord(
                    1, string.Empty, "actor-1", captured.Snapshot.ObjectGeneration,
                    captured.Snapshot.AuthorityOwnerGeneration),
                sourceRid, 1, requiredMessages, requiredBytes, [participant],
                new ZLinkServiceWireCodec.RelocationRootRecord("root", 3), 1);
            permits ??= new ZLinkRelocationPermitPool(new ZLinkLocationOptions());
            var owner = new ZLinkCanonicalRelocationReservationOwner(
                store, permits, "mesh", targetRid, 1,
                TimeSpan.FromSeconds(5), time);
            return new Fixture(store, permits, owner, time, sourceRid,
                coordinatorLease.Token, key, prepare);
        }

        internal ZLinkServiceWireCodec.RelocationReadyRecord Acceptance(
            ZLinkServiceWireCodec.RelocationReadyRecord offer) => offer with
        {
            Role = 1,
            OfferedMessages = 0,
            OfferedBytes = 0,
            Participants = Prepare.Participants
        };

        public ValueTask DisposeAsync() => Owner.DisposeAsync();

        internal static ZLinkMeshNodeDescriptor Descriptor(
            RoutingId rid,
            ZLinkLocationOwnerToken owner,
            ulong lifecycleGeneration = 1) => new(
            "mesh", rid, lifecycleGeneration, 1, $"inproc://{rid.ToHex()}",
            new Dictionary<string, int>(StringComparer.Ordinal) { ["mesh"] = 100 },
            string.Empty, owner.OwnerId, owner.LeaseGeneration,
            DateTimeOffset.UtcNow)
        {
            ObjectRole = ZLinkMeshNodeObjectRole.Server,
            ObjectCapabilities =
            [
                new ZLinkObjectCapability(ZLinkPlacementObjectKind.Actor,
                    "Game.Actor", ZLinkObjectMaintenancePolicyKind.Recreate,
                    false, 0)
            ],
            State = ZLinkFrameworkRuntimeState.Serving,
            EntrySpotId = $"{rid.ToHex()}-entry-00000000-0000-4000-8000-000000000001",
            Capacity = new ZLinkPlacementCapacity(
                new ZLinkPopulationCapacity(0, 0, 100),
                new ZLinkPopulationCapacity(0, 0, 100), [])
        };
    }

    private static ZLinkManagedMeshNode NewNode(
        IContext context,
        string name,
        TimeSpan? inboundOperationShutdownTimeout = null)
    {
        var node = new ZLinkManagedMeshNode(
            context,
            "mesh",
            inboundOperationShutdownTimeout: inboundOperationShutdownTimeout);
        node.SetRoutingId(RoutingId.From(name));
        node.AddChannel("mesh");
        return node;
    }

    private static async Task WaitUntilAsync(Func<bool> predicate)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (!predicate())
        {
            if (DateTime.UtcNow >= deadline) throw new TimeoutException();
            await Task.Delay(10);
        }
    }

    private sealed class BlockingAuthorityStore(IZLinkLocationRepository inner)
        : ZLinkLocationStoreTestDouble
    {
        internal TaskCompletionSource Entered { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        internal TaskCompletionSource Release { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        internal int ReserveCalls { get; private set; }

        public override ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
            ZLinkAuthorityKey key, CancellationToken cancellationToken = default) =>
            inner.ReadAuthorityAsync(key, cancellationToken);

        public override ValueTask<ZLinkAuthorityCompareExchangeResult>
            CompareExchangeAuthorityAsync(ZLinkAuthorityKey key,
                string expectedStoreVersion, ZLinkAuthorityMutation mutation,
                CancellationToken cancellationToken = default) =>
            inner.CompareExchangeAuthorityAsync(key, expectedStoreVersion,
                mutation, cancellationToken);

        public override ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
            string prefix, ZLinkAuthorityScanCursor? cursor, int limit,
            CancellationToken cancellationToken = default) =>
            inner.ListAuthoritiesAsync(prefix, cursor, limit, cancellationToken);

        public override ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            inner.ReadOwnerLeaseAsync(ownerId, cancellationToken);

        public override ValueTask<ZLinkLocationPage<ZLinkMeshNodeDescriptor>>
            ListMeshNodesAsync(
                string meshName,
                ZLinkPageRequest page,
                CancellationToken cancellationToken = default) =>
            inner.ListMeshNodesAsync(meshName, page, cancellationToken);

        public override ValueTask<ZLinkObjectReserveResult> ReserveAsync(
            ZLinkObjectReservationRequest request,
            CancellationToken cancellationToken = default) =>
            inner.ReserveAsync(request, cancellationToken);

        public override ValueTask<ZLinkObjectCommitResult> CommitAsync(
            ZLinkObjectReservation reservation, ReadOnlyMemory<byte> readyPayload,
            CancellationToken cancellationToken = default) =>
            inner.CommitAsync(reservation, readyPayload, cancellationToken);

        public override ValueTask<ZLinkObjectCreationCompleteResult> CompleteCreationAsync(
            ZLinkObjectReservation reservation,
            ZLinkObjectCreationCompletion completion,
            CancellationToken cancellationToken = default) =>
            inner.CompleteCreationAsync(reservation, completion, cancellationToken);

        public override ValueTask<ZLinkCreationTerminalReadResult> ReadCreationTerminalAsync(
            ZLinkCreationOperationId operation,
            CancellationToken cancellationToken = default) =>
            inner.ReadCreationTerminalAsync(operation, cancellationToken);

        public override ValueTask<ZLinkObjectAbortResult> AbortAsync(
            ZLinkObjectReservation reservation,
            CancellationToken cancellationToken = default) =>
            inner.AbortAsync(reservation, cancellationToken);

        public override async ValueTask<ZLinkRelocationCapacityReserveResult>
            ReserveRelocationCapacityAsync(
                ZLinkRelocationCapacityReservationRequest request,
                CancellationToken cancellationToken = default)
        {
            ReserveCalls++;
            Entered.TrySetResult();
            await Release.Task.WaitAsync(cancellationToken);
            return await inner.ReserveRelocationCapacityAsync(request,
                cancellationToken);
        }

        public override ValueTask<ZLinkRelocationCapacityAbortResult>
            AbortRelocationCapacityAsync(ZLinkRelocationCapacityFence fence,
                CancellationToken cancellationToken = default) =>
            inner.AbortRelocationCapacityAsync(fence, cancellationToken);

        public override ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
            ZLinkAggregatePrepareRequest request,
            CancellationToken cancellationToken = default) =>
            inner.PrepareAggregateAsync(request, cancellationToken);

        public override ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default) =>
            inner.CommitAggregateAsync(fence, cancellationToken);

        public override ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default) =>
            inner.AbortAggregateAsync(fence, cancellationToken);
    }

    private sealed class RawAttemptTarget : ICanonicalRelocationReservationTarget
    {
        private bool _sealRequested;

        internal RawAttemptTarget(RoutingId sourceRid,
            ulong sourceGeneration, RoutingId targetRid,
            ulong targetGeneration)
        {
            var participant =
                new ZLinkServiceWireCodec.RelocationParticipantRecord(
                    1, 1, default, 0, null, 0, default, 0, 1, 256);
            Prepare = new ZLinkServiceWireCodec.RelocationPrepareRecord(
                new ZLinkServiceWireCodec.RelocationWireId(91, 92),
                1, 1,
                new ZLinkServiceWireCodec.RelocationCoordinatorFence(
                    "source-owner", 1, sourceRid, sourceGeneration,
                    "authority-version"),
                new ZLinkServiceWireCodec.RelocationCandidateRecord(
                    targetRid, targetGeneration, "target-owner", 1),
                1,
                new ZLinkServiceWireCodec.RelocationObjectRecord(
                    1, string.Empty, "actor-1", 1, 1),
                sourceRid, sourceGeneration, 1, 256, [participant],
                new ZLinkServiceWireCodec.RelocationRootRecord("root", 1), 1);
        }

        internal ZLinkServiceWireCodec.RelocationPrepareRecord Prepare { get; }
        internal int DataRecords { get; private set; }
        internal int SealResponses { get; private set; }
        internal int Completions { get; private set; }

        public ValueTask<ZLinkServiceWireCodec.RelocationReadyRecord> OfferAsync(
            ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken) => ValueTask.FromResult(
            new ZLinkServiceWireCodec.RelocationReadyRecord(
                prepare.RelocationId, prepare.TargetAttemptGeneration,
                prepare.RoundKind, prepare.Coordinator, prepare.Candidate,
                prepare.Object, 2, 64, 4096, [],
                prepare.SourceNodeGeneration,
                prepare.Candidate.NodeGeneration, 1, prepare.Root,
                prepare.ApplicationVersion,
                prepare.Participants.Select(participant =>
                    new ZLinkServiceWireCodec.RelocationParticipantProgressRecord(
                        participant.ParticipantId, 0, 0)).ToArray()));

        public ValueTask<ZLinkServiceWireCodec.RelocationReservedRecord> AcceptAsync(
            ZLinkServiceWireCodec.RelocationReadyRecord acceptance,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken) => ValueTask.FromResult(
            new ZLinkServiceWireCodec.RelocationReservedRecord(
                acceptance.RelocationId,
                acceptance.TargetAttemptGeneration,
                acceptance.RoundKind,
                acceptance.Coordinator,
                acceptance.Candidate,
                acceptance.ReservationGeneration,
                acceptance.Participants));

        public ValueTask<ZLinkServiceWireCodec.RelocationAckRecord> StageDataAsync(
            ZLinkServiceWireCodec.RelocationDataRecord data,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken)
        {
            DataRecords++;
            return ValueTask.FromResult(
                new ZLinkServiceWireCodec.RelocationAckRecord(
                    data.RelocationId, data.TargetAttemptGeneration,
                    data.Coordinator, 2, data.ParticipantId, data.Sequence));
        }

        public bool TryCreateSealRequest(
            ZLinkServiceWireCodec.RelocationWireId relocationId,
            ulong targetAttemptGeneration,
            out ZLinkServiceWireCodec.RelocationSealRecord seal)
        {
            if (_sealRequested || DataRecords == 0)
            {
                seal = null!;
                return false;
            }
            _sealRequested = true;
            seal = new ZLinkServiceWireCodec.RelocationSealRecord(
                relocationId, targetAttemptGeneration,
                Prepare.Coordinator, 2, false, []);
            return true;
        }

        public ValueTask AcceptSealResponseAsync(
            ZLinkServiceWireCodec.RelocationSealRecord seal,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken)
        {
            SealResponses++;
            return ValueTask.CompletedTask;
        }

        public ValueTask CompleteAsync(
            ZLinkServiceWireCodec.RelocationCompleteRecord complete,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken)
        {
            Completions++;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class BlockingOfferTarget : ICanonicalRelocationReservationTarget
    {
        private readonly RawAttemptTarget _inner;
        private int _offerCount;

        internal BlockingOfferTarget(
            RoutingId sourceRid,
            ulong sourceGeneration,
            RoutingId targetRid,
            ulong targetGeneration) =>
            _inner = new RawAttemptTarget(
                sourceRid, sourceGeneration, targetRid, targetGeneration);

        internal ZLinkServiceWireCodec.RelocationPrepareRecord Prepare =>
            _inner.Prepare;

        internal TaskCompletionSource Entered { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal TaskCompletionSource CancellationObserved { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal TaskCompletionSource AllowExit { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal TaskCompletionSource Exited { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        internal int OfferCount => Volatile.Read(ref _offerCount);

        public async ValueTask<ZLinkServiceWireCodec.RelocationReadyRecord> OfferAsync(
            ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken)
        {
            Interlocked.Increment(ref _offerCount);
            Entered.TrySetResult();
            using var registration = cancellationToken.Register(
                () => CancellationObserved.TrySetResult());
            try
            {
                await AllowExit.Task.ConfigureAwait(false);
                return await _inner.OfferAsync(
                        prepare,
                        authenticatedSourceNodeRid,
                        CancellationToken.None)
                    .ConfigureAwait(false);
            }
            finally
            {
                Exited.TrySetResult();
            }
        }

        public ValueTask<ZLinkServiceWireCodec.RelocationReservedRecord> AcceptAsync(
            ZLinkServiceWireCodec.RelocationReadyRecord acceptance,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken) =>
            _inner.AcceptAsync(acceptance, authenticatedSourceNodeRid, cancellationToken);

        public ValueTask<ZLinkServiceWireCodec.RelocationAckRecord> StageDataAsync(
            ZLinkServiceWireCodec.RelocationDataRecord data,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken) =>
            _inner.StageDataAsync(data, authenticatedSourceNodeRid, cancellationToken);

        public bool TryCreateSealRequest(
            ZLinkServiceWireCodec.RelocationWireId relocationId,
            ulong targetAttemptGeneration,
            out ZLinkServiceWireCodec.RelocationSealRecord seal) =>
            _inner.TryCreateSealRequest(relocationId, targetAttemptGeneration, out seal);

        public ValueTask AcceptSealResponseAsync(
            ZLinkServiceWireCodec.RelocationSealRecord response,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken) =>
            _inner.AcceptSealResponseAsync(response, authenticatedSourceNodeRid, cancellationToken);

        public ValueTask CompleteAsync(
            ZLinkServiceWireCodec.RelocationCompleteRecord complete,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken) =>
            _inner.CompleteAsync(complete, authenticatedSourceNodeRid, cancellationToken);
    }
}
