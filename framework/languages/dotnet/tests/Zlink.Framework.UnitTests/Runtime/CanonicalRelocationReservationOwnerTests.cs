using System.Buffers.Binary;
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
    public async Task Instance_command40_without_wire_owner_generation_accepts_exact_ready_authority()
    {
        await using var fixture = await Fixture.CreateAsync();
        var prepare = await fixture.CreateInstancePrepareAsync();
        var offer = await fixture.Owner.OfferAsync(
            prepare, fixture.SourceRid, CancellationToken.None);

        var reserved = await fixture.Owner.AcceptAsync(
            fixture.Acceptance(offer, prepare),
            fixture.SourceRid,
            CancellationToken.None);

        Assert.Equal(prepare.RelocationId, reserved.RelocationId);
        Assert.Equal(0UL, prepare.Object.ExpectedAuthorityOwnerGeneration);
    }

    [Theory]
    [InlineData(true, false)]
    [InlineData(false, true)]
    public async Task Instance_command40_rejects_stale_ready_authority_fence(
        bool staleNode,
        bool staleOwner)
    {
        await using var fixture = await Fixture.CreateAsync();
        var prepare = await fixture.CreateInstancePrepareAsync(
            staleNode,
            staleOwner);
        var offer = await fixture.Owner.OfferAsync(
            prepare, fixture.SourceRid, CancellationToken.None);

        await Assert.ThrowsAsync<InvalidDataException>(() => fixture.Owner
            .AcceptAsync(
                fixture.Acceptance(offer, prepare),
                fixture.SourceRid,
                CancellationToken.None)
            .AsTask());
    }

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

    [Theory]
    [InlineData(1, true)]
    [InlineData(2, false)]
    [InlineData(3, false)]
    public void Steady_authority_only_proves_standalone_actor_local_completion(
        byte objectKind,
        bool expected)
    {
        Assert.Equal(
            expected,
            ZLinkCanonicalRelocationReservationOwner
                .IsLocalTargetCompletionProvenBySteadyAuthority(objectKind));
    }

    [Fact]
    public void Canonical_spot_publication_accepts_a_successor_progress_root()
    {
        var prepared = new ZLinkServiceWireCodec.RelocationRootRecord(
            "prepared-root",
            17);

        Assert.True(
            ZLinkCanonicalRelocationReservationOwner.IsExactPublicationRoot(
                2,
                prepared,
                "successor-progress-root",
                23));
        Assert.True(
            ZLinkCanonicalRelocationReservationOwner.IsExactPublicationRoot(
                3,
                prepared,
                "successor-progress-root",
                23));
        Assert.False(
            ZLinkCanonicalRelocationReservationOwner.IsExactPublicationRoot(
                2,
                prepared,
                string.Empty,
                0));
        Assert.False(
            ZLinkCanonicalRelocationReservationOwner.IsExactPublicationRoot(
                1,
                prepared,
                "successor-progress-root",
                23));
        Assert.True(
            ZLinkCanonicalRelocationReservationOwner.IsExactPublicationRoot(
                1,
                prepared,
                prepared.Reference,
                prepared.ChecksumCrc32c));
    }

    [Fact]
    public void Steady_user_spot_authority_does_not_cross_decode_as_relocation()
    {
        var steady = ZLinkUserSpotAuthorityPayloadCodec.Encode(
            new ZLinkUserSpotAuthorityPayload(
                ZLinkUserSpotAuthorityState.Ready,
                "Game.Room",
                "room-1",
                "owner-1",
                2,
                "mesh",
                RoutingId.From("node-1"),
                3));

        Assert.True(
            ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                steady,
                out _));
        Assert.False(
            ZLinkCanonicalRelocationAuthorityStateCodec.TryRead(
                steady,
                out _));
        Assert.False(
            ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                steady,
                out _));
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
    public async Task UserSpot_accept_uses_one_aggregate_prepare_without_standalone_capacity()
    {
        var time = new ManualTimeProvider();
        var inner = new ZLinkInMemoryLocationStore(time);
        var store = new BlockingAuthorityStore(inner);
        var relocation = new DurableReceiptStore(time);
        var sourceRid = RoutingId.From("userspot-source");
        var targetRid = RoutingId.From("userspot-target");
        var sourceLease = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await inner.ClaimOwnerLeaseAsync(
                "userspot-source-owner",
                TimeSpan.FromMinutes(1)));
        var targetLease = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await inner.ClaimOwnerLeaseAsync(
                "userspot-target-owner",
                TimeSpan.FromMinutes(1)));
        var sourceDescriptor = Fixture.Descriptor(
            sourceRid,
            sourceLease.Token) with
        {
            ObjectCapabilities =
            [
                new ZLinkObjectCapability(
                    ZLinkPlacementObjectKind.UserSpot,
                    "Game.Room",
                    ZLinkObjectMaintenancePolicyKind.Snapshot,
                    true,
                    100)
            ],
            Capacity = new ZLinkPlacementCapacity(
                new ZLinkPopulationCapacity(0, 0, 100),
                new ZLinkPopulationCapacity(0, 0, 100),
                [
                    new ZLinkSpotTypeCapacity(
                        ZLinkPlacementObjectKind.UserSpot,
                        "Game.Room",
                        0,
                        0,
                        100)
                ])
        };
        var targetDescriptor = Fixture.Descriptor(
            targetRid,
            targetLease.Token) with
        {
            ObjectCapabilities = sourceDescriptor.ObjectCapabilities,
            Capacity = sourceDescriptor.Capacity
        };
        await inner.UpdateMeshNodeAsync(
            sourceDescriptor,
            ZLinkLocationWriteIntent.NewClaim);
        await inner.UpdateMeshNodeAsync(
            targetDescriptor,
            ZLinkLocationWriteIntent.NewClaim);
        var key = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey("room-1");
        var reservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await inner.ReserveAsync(
                new ZLinkObjectReservationRequest(
                    ZLinkPlacementObjectKind.UserSpot,
                    key,
                    "Game.Room",
                    "intent",
                    SHA256.HashData([0x41]),
                    1,
                    new ZLinkMeshNodeDescriptorKey("mesh", sourceRid),
                    1,
                    sourceLease.Token,
                    new byte[] { 0x42 },
                    new ZLinkCapacityVector(
                        0,
                        1,
                        new ZLinkSpotTypeCapacityDelta(
                            ZLinkPlacementObjectKind.UserSpot,
                            "Game.Room",
                            1)))));
        var sourceAuthority =
            ZLinkUserSpotAuthorityPayloadCodec.Encode(
                new ZLinkUserSpotAuthorityPayload(
                    ZLinkUserSpotAuthorityState.Ready,
                    "Game.Room",
                    "room-1",
                    sourceLease.Token.OwnerId,
                    checked((ulong)sourceLease.Token.LeaseGeneration),
                    "mesh",
                    sourceRid,
                    1));
        var ready = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await inner.CommitAsync(
                reservation.Reservation,
                sourceAuthority)).Snapshot;
        var recovery = ZLinkCanonicalParticipantRecoveryCodec.Encode(
            new ZLinkCanonicalParticipantRecovery(
                key,
                ZLinkPlacementObjectKind.UserSpot,
                ready.ObjectGeneration,
                ready.AuthorityOwnerGeneration,
                ready.StoreVersion,
                "Game.Room",
                sourceAuthority,
                ReadOnlyMemory<byte>.Empty));
        var sourceParticipant = new ZLinkRelocationParticipantEnvelope(
            key,
            ZLinkPlacementObjectKind.UserSpot,
            ready.ObjectGeneration,
            ready.AuthorityOwnerGeneration,
            new byte[] { 0x51 },
            [],
            [],
            recovery,
            ZLinkSpotRetireCompletionMarker.CreatePending());
        var sourceRequestParticipant =
            new ZLinkAggregateRelocationParticipant(
                sourceParticipant,
                ready.StoreVersion,
                ZLinkAuthorityGenerationTransition.NewOwner,
                sourceAuthority,
                ReadOnlyMemory<byte>.Empty);
        var source = new ZLinkRelocationEnvelope(
            Guid.NewGuid(),
            1,
            ZLinkAggregateInventoryDigest.Compute(
                [sourceRequestParticipant]),
            [sourceParticipant]);
        var canonical = ZLinkCanonicalSpotRelocationWriter.CreateInitial(
            source,
            "room-1",
            "Game.Room",
            targetRid,
            1);
        var stored = await ZLinkRelocationTreeStore.PutAsync(
            relocation,
            canonical,
            TimeSpan.FromHours(24),
            CancellationToken.None);
        var participant =
            new ZLinkServiceWireCodec.RelocationParticipantRecord(
                1,
                1,
                default,
                0,
                null,
                0,
                default,
                0,
                0,
                0);
        var prepare = new ZLinkServiceWireCodec.RelocationPrepareRecord(
            new ZLinkServiceWireCodec.RelocationWireId(
                canonical.CanonicalRelocationHigh,
                canonical.CanonicalRelocationLow),
            1,
            1,
            new ZLinkServiceWireCodec.RelocationCoordinatorFence(
                sourceLease.Token.OwnerId,
                checked((ulong)sourceLease.Token.LeaseGeneration),
                sourceRid,
                1,
                ready.StoreVersion),
            new ZLinkServiceWireCodec.RelocationCandidateRecord(
                targetRid,
                1,
                targetLease.Token.OwnerId,
                checked((ulong)targetLease.Token.LeaseGeneration)),
            1,
            new ZLinkServiceWireCodec.RelocationObjectRecord(
                2,
                "Game.Room",
                "room-1",
                ready.ObjectGeneration,
                ready.AuthorityOwnerGeneration),
            sourceRid,
            1,
            0,
            checked((ulong)ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(
                canonical)),
            [participant],
            new ZLinkServiceWireCodec.RelocationRootRecord(
                stored.Root.Reference,
                stored.Root.ChecksumCrc32c),
            1);
        var permits = new ZLinkRelocationPermitPool(
            new ZLinkLocationOptions());
        await using var owner =
            new ZLinkCanonicalRelocationReservationOwner(
                store,
                permits,
                "mesh",
                targetRid,
                1,
                TimeSpan.FromSeconds(5),
                time,
                relocationStore: relocation);

        var offer = await owner.OfferAsync(
            prepare,
            sourceRid,
            CancellationToken.None);
        Assert.Empty(offer.Participants);
        var acceptance = offer with
        {
            Role = 1,
            OfferedMessages = 0,
            OfferedBytes = 0,
            Participants = prepare.Participants
        };
        var reserved = await owner.AcceptAsync(
            acceptance,
            sourceRid,
            CancellationToken.None);
        var duplicate = await owner.AcceptAsync(
            acceptance,
            sourceRid,
            CancellationToken.None);

        Assert.Equal(reserved, duplicate);
        Assert.Equal(1, store.AggregatePrepareCalls);
        Assert.Equal(0, store.ReserveCalls);
        await Assert.ThrowsAsync<InvalidDataException>(
            async () => await owner.AcceptAsync(
                acceptance with { Participants = [] },
                sourceRid,
                CancellationToken.None));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task Raw_two_node_command35_retries_until_target_ack(
        bool loseFirstCompletionAck)
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
            target.Status().LifecycleGeneration,
            loseFirstCompletionAck);
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
        var expectedCompletions = loseFirstCompletionAck ? 2 : 1;
        await WaitUntilAsync(
            () => attempt.Completions == expectedCompletions);
        var terminalSealResponses = attempt.SealResponses;
        await Task.Delay(250);

        Assert.Equal(1, attempt.DataRecords);
        Assert.True(terminalSealResponses >= 2);
        Assert.Equal(terminalSealResponses, attempt.SealResponses);
        Assert.Equal(expectedCompletions, attempt.Completions);
    }

    [Fact]
    public async Task Raw_two_node_zero_journal_spot_reaches_seal_without_data()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "zero-journal-source");
        await using var target = NewNode(context, "zero-journal-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://zero-journal-source-{suffix}";
        var targetEndpoint = $"inproc://zero-journal-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        var attempt = new RawAttemptTarget(
            source.RoutingId,
            source.Status().LifecycleGeneration,
            target.RoutingId,
            target.Status().LifecycleGeneration,
            objectKind: 2,
            allowanceMessages: 0);
        target.SetCanonicalRelocationReservationTarget(attempt);
        source.Start();
        target.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 1
            && target.Status().AdmittedPeerCount == 1);

        await source.StageCanonicalRelocationAsync(
            target.RoutingId,
            attempt.Prepare,
            [],
            TimeSpan.FromSeconds(5),
            CancellationToken.None);
        await WaitUntilAsync(() => attempt.SealResponses >= 1);

        Assert.Equal(0, attempt.DataRecords);
    }

    [Fact]
    public async Task Raw_two_node_instance_spot_offer_matches_the_command40_shape()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "instance-offer-source");
        await using var target = NewNode(context, "instance-offer-target");
        var suffix = Guid.NewGuid().ToString("N");
        var targetEndpoint = $"inproc://instance-offer-target-{suffix}";
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        var attempt = new RawAttemptTarget(
            source.RoutingId,
            source.Status().LifecycleGeneration,
            target.RoutingId,
            target.Status().LifecycleGeneration,
            objectKind: 3,
            allowanceMessages: 0);
        target.SetCanonicalRelocationReservationTarget(attempt);
        target.Start();
        source.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 1
            && target.Status().AdmittedPeerCount == 1);

        await source.StageCanonicalRelocationAsync(
            target.RoutingId,
            attempt.Prepare,
            [],
            TimeSpan.FromSeconds(5),
            CancellationToken.None);

        Assert.Equal(1, attempt.Offers);
        Assert.Equal(0UL,
            attempt.Prepare.Object.ExpectedAuthorityOwnerGeneration);
    }

    [Fact]
    public async Task Capacity_failure_releases_pending_before_the_next_candidate()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "capacity-source");
        await using var first = NewNode(context, "capacity-first");
        await using var second = NewNode(context, "capacity-second");
        var suffix = Guid.NewGuid().ToString("N");
        var firstEndpoint = $"inproc://capacity-first-{suffix}";
        var secondEndpoint = $"inproc://capacity-second-{suffix}";
        first.SetBind(firstEndpoint);
        second.SetBind(secondEndpoint);
        source.ConnectPeer(firstEndpoint, first.RoutingId);
        source.ConnectPeer(secondEndpoint, second.RoutingId);
        var rejected = new RawAttemptTarget(
            source.RoutingId,
            source.Status().LifecycleGeneration,
            first.RoutingId,
            first.Status().LifecycleGeneration,
            rejectOffer: true);
        var accepted = new RawAttemptTarget(
            source.RoutingId,
            source.Status().LifecycleGeneration,
            second.RoutingId,
            second.Status().LifecycleGeneration);
        first.SetCanonicalRelocationReservationTarget(rejected);
        second.SetCanonicalRelocationReservationTarget(accepted);
        first.Start();
        second.Start();
        source.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 2
            && first.Status().AdmittedPeerCount == 1
            && second.Status().AdmittedPeerCount == 1);

        var failure = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            () => source.ReserveCanonicalRelocationAsync(
                first.RoutingId,
                rejected.Prepare,
                TimeSpan.FromMilliseconds(100),
                CancellationToken.None).AsTask());
        Assert.Equal(ZLinkFrameworkErrorKind.DeadlineExceeded, failure.Kind);
        await WaitUntilAsync(
            () => source.PendingCanonicalRelocationReservationCount == 0);

        var reserved = await source.ReserveCanonicalRelocationAsync(
            second.RoutingId,
            accepted.Prepare,
            TimeSpan.FromSeconds(5),
            CancellationToken.None);

        Assert.Equal(accepted.Prepare.RelocationId, reserved.RelocationId);
        await WaitUntilAsync(
            () => source.PendingCanonicalRelocationReservationCount == 0);
    }

    [Fact]
    public async Task Lifecycle_mismatch_does_not_retain_a_pending_reservation()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "lifecycle-source");
        await using var target = NewNode(context, "lifecycle-target");
        var suffix = Guid.NewGuid().ToString("N");
        var targetEndpoint = $"inproc://lifecycle-target-{suffix}";
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        var attempt = new RawAttemptTarget(
            source.RoutingId,
            source.Status().LifecycleGeneration,
            target.RoutingId,
            target.Status().LifecycleGeneration);
        target.SetCanonicalRelocationReservationTarget(attempt);
        target.Start();
        source.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 1
            && target.Status().AdmittedPeerCount == 1);

        var mismatch = attempt.Prepare with
        {
            Candidate = attempt.Prepare.Candidate with
            {
                NodeGeneration =
                    checked(attempt.Prepare.Candidate.NodeGeneration + 1)
            }
        };
        var failure = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            () => source.ReserveCanonicalRelocationAsync(
                target.RoutingId,
                mismatch,
                TimeSpan.FromSeconds(5),
                CancellationToken.None).AsTask());
        Assert.Equal(ZLinkFrameworkErrorKind.Unavailable, failure.Kind);
        Assert.Equal(0, source.PendingCanonicalRelocationReservationCount);

        _ = await source.ReserveCanonicalRelocationAsync(
            target.RoutingId,
            attempt.Prepare,
            TimeSpan.FromSeconds(5),
            CancellationToken.None);
        await WaitUntilAsync(
            () => source.PendingCanonicalRelocationReservationCount == 0);
    }

    [Fact]
    public async Task One_sided_route_connection_accepts_command40_source_fence()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "one-sided-source");
        await using var target = NewNode(context, "one-sided-target");
        var suffix = Guid.NewGuid().ToString("N");
        var targetEndpoint = $"inproc://one-sided-target-{suffix}";
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        var attempt = new RawAttemptTarget(
            source.RoutingId,
            source.Status().LifecycleGeneration,
            target.RoutingId,
            target.Status().LifecycleGeneration,
            objectKind: 2,
            allowanceMessages: 0);
        target.SetCanonicalRelocationReservationTarget(attempt);
        target.Start();
        source.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 1
            && target.Status().AdmittedPeerCount == 1);

        await source.StageCanonicalRelocationAsync(
            target.RoutingId,
            attempt.Prepare,
            [],
            TimeSpan.FromSeconds(5),
            CancellationToken.None);

        Assert.Equal(1, attempt.Offers);
        Assert.True(attempt.SealResponses >= 1);
    }

    [Fact]
    public async Task Source_stage_waits_for_target_seal_completion_ack()
    {
        await using var context = Systems.Zlink.Zlink.CreateContext();
        await using var source = NewNode(context, "seal-ack-source");
        await using var target = NewNode(context, "seal-ack-target");
        var suffix = Guid.NewGuid().ToString("N");
        var sourceEndpoint = $"inproc://seal-ack-source-{suffix}";
        var targetEndpoint = $"inproc://seal-ack-target-{suffix}";
        source.SetBind(sourceEndpoint);
        target.SetBind(targetEndpoint);
        source.ConnectPeer(targetEndpoint, target.RoutingId);
        target.ConnectPeer(sourceEndpoint, source.RoutingId);
        var targetCompletion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var attempt = new RawAttemptTarget(
            source.RoutingId,
            source.Status().LifecycleGeneration,
            target.RoutingId,
            target.Status().LifecycleGeneration,
            objectKind: 2,
            allowanceMessages: 0,
            sealCompletion: targetCompletion.Task);
        target.SetCanonicalRelocationReservationTarget(attempt);
        source.Start();
        target.Start();
        await WaitUntilAsync(() => source.Status().AdmittedPeerCount == 1
            && target.Status().AdmittedPeerCount == 1);

        var staging = source.StageCanonicalRelocationAsync(
                target.RoutingId,
                attempt.Prepare,
                [],
                TimeSpan.FromSeconds(5),
                CancellationToken.None)
            .AsTask();
        await WaitUntilAsync(() => attempt.SealResponses >= 1);

        Assert.False(staging.IsCompleted);
        targetCompletion.TrySetResult();
        await staging;
    }

    [Theory]
    [InlineData("terminal-eviction", 0, false, true, true)]
    [InlineData("target-restart", 1, false, true, false)]
    [InlineData("newer-lifecycle", 2, false, true, false)]
    [InlineData("receipt-expired", 0, true, true, false)]
    [InlineData("first-command", 0, false, false, false)]
    [InlineData("marker-write-failure", 0, false, false, true)]
    public async Task Command35_durable_receipt_fences_lifecycle_expiry_and_first_effect(
        string scenario,
        int lifecycleDelta,
        bool expireReceipt,
        bool writeApplied,
        bool expectedSuccess)
    {
        _ = scenario;
        await using var fixture = await Fixture.CreateAsync();
        var durableTime = new ManualTimeProvider();
        var durable = new DurableReceiptStore(durableTime);
        var targetRid = RoutingId.From("durable-receipt-target");
        var rootPayload = new byte[] { 9, 8, 7 };
        var rawPrepare = new RawAttemptTarget(
            fixture.SourceRid,
            1,
            targetRid,
            1).Prepare;
        var prepare = rawPrepare with
        {
            Object = rawPrepare.Object with { StableType = "actor-type" },
            Root = new ZLinkServiceWireCodec.RelocationRootRecord(
                "durable-root",
                ZLinkCrc32C.Compute(rootPayload))
        };
        var keyPrefix =
            $"zlink-completion-{prepare.RelocationId.High:x16}"
            + $"-{prepare.RelocationId.Low:x16}"
            + $"-{prepare.TargetAttemptGeneration:x16}";
        var complete = new ZLinkServiceWireCodec.RelocationCompleteRecord(
            prepare.RelocationId,
            prepare.TargetAttemptGeneration,
            prepare.Coordinator,
            1,
            new ZLinkServiceWireCodec.RequestSourceFence(
                prepare.Coordinator.OwnerId,
                prepare.Coordinator.LeaseGeneration,
                prepare.SourceNodeRid,
                prepare.SourceNodeGeneration),
            1);
        var fingerprint = SHA256.HashData(
            ZLinkServiceWireCodec.EncodeRelocationComplete(complete));
        await durable.PutRelocationAtAsync(
            prepare.Root.Reference,
            rootPayload,
            TimeSpan.FromHours(24));
        await durable.PutRelocationAtAsync(
            $"{keyPrefix}-terminal",
            ZLinkCanonicalRelocationReservationOwner.EncodeTerminalReceipt(
                prepare,
                prepare.Candidate.NodeGeneration,
                prepare.Object.ExpectedAuthorityOwnerGeneration + 1),
            TimeSpan.FromHours(24));
        await durable.PutRelocationAtAsync(
            $"{keyPrefix}-command35",
            fingerprint,
            TimeSpan.FromHours(24));
        if (writeApplied)
            await durable.PutRelocationAtAsync(
                $"{keyPrefix}-applied",
                fingerprint,
                TimeSpan.FromHours(24));
        var steadyAuthorityPayload = ZLinkActorAuthorityPayloadCodec.Encode(
            new ZLinkActorAuthorityPayload(
                ZLinkActorAuthorityState.Ready,
                prepare.Object.StableType,
                prepare.Object.ObjectId,
                "entry-spot",
                1,
                ZLinkSpotKind.Entry,
                prepare.Candidate.OwnerId,
                prepare.Candidate.OwnerLeaseGeneration,
                "mesh",
                prepare.Candidate.NodeRid,
                prepare.Candidate.NodeGeneration));
        var aggregateIdBytes = new byte[16];
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(
            aggregateIdBytes,
            prepare.RelocationId.High);
        System.Buffers.Binary.BinaryPrimitives.WriteUInt64BigEndian(
            aggregateIdBytes.AsSpan(8),
            prepare.RelocationId.Low);
        var canonicalRoot = new ZLinkRelocationEnvelope(
            new Guid(aggregateIdBytes, bigEndian: true),
            1,
            new byte[32],
            [])
        {
            CanonicalLogicalStream = new byte[] { 1 },
            CanonicalRelocationHigh = prepare.RelocationId.High,
            CanonicalRelocationLow = prepare.RelocationId.Low,
            CanonicalApplicationVersion =
                checked((long)prepare.ApplicationVersion)
        };
        var authorityPayload = scenario == "marker-write-failure"
            ? steadyAuthorityPayload
            : ZLinkCanonicalRelocationAuthorityStateCodec
                .ReplaceRelocationState(
                    steadyAuthorityPayload,
                    new ZLinkCanonicalRelocationAuthorityState(
                        prepare.RelocationId.High,
                        prepare.RelocationId.Low,
                        prepare.TargetAttemptGeneration,
                        prepare.SourceNodeRid.ToHex(),
                        prepare.SourceNodeGeneration,
                        prepare.Coordinator.OwnerId,
                        prepare.Coordinator.LeaseGeneration,
                        prepare.Candidate.NodeRid.ToHex(),
                        prepare.Candidate.NodeGeneration,
                        prepare.Candidate.OwnerId,
                        prepare.Candidate.OwnerLeaseGeneration,
                        1,
                        prepare.Coordinator.OwnerId,
                        prepare.Coordinator.LeaseGeneration,
                        prepare.Coordinator.NodeRid.ToHex(),
                        prepare.Coordinator.NodeGeneration,
                        8,
                        prepare.Root.Reference,
                        prepare.Root.ChecksumCrc32c,
                        checked((long)prepare.ApplicationVersion),
                        1),
                    canonicalRoot);
        var authoritySnapshot = new ZLinkAuthoritySnapshot(
                "published",
                authorityPayload,
                prepare.Object.ObjectGeneration,
                prepare.Object.ExpectedAuthorityOwnerGeneration + 1,
                prepare.Candidate.OwnerId,
                checked((long)prepare.Candidate.OwnerLeaseGeneration),
                new ZLinkPlacementAllocation(
                    ZLinkPlacementAllocationState.Active,
                    ZLinkPlacementObjectKind.Actor,
                    prepare.Object.StableType,
                    new ZLinkMeshNodeDescriptorKey(
                        "mesh",
                        prepare.Candidate.NodeRid),
                    prepare.Candidate.NodeGeneration,
                    new ZLinkCapacityVector(1, 0, null)),
                null,
                DateTimeOffset.UtcNow);
        var authorityStore = new DurableAuthorityStore(authoritySnapshot);
        await using var recovered =
            new ZLinkCanonicalRelocationReservationOwner(
                authorityStore,
                fixture.Permits,
                "mesh",
                prepare.Candidate.NodeRid,
                prepare.Candidate.NodeGeneration
                + checked((ulong)lifecycleDelta),
                TimeSpan.FromSeconds(5),
                relocationStore: durable);

        if (expireReceipt)
            durableTime.Advance(TimeSpan.FromHours(25));
        if (scenario == "marker-write-failure")
        {
            durable.FailNextPutReference = $"{keyPrefix}-applied";
            await Assert.ThrowsAsync<IOException>(
                () => recovered.CompleteAsync(
                        complete,
                        prepare.SourceNodeRid,
                        CancellationToken.None)
                    .AsTask());
            await recovered.CompleteAsync(
                complete,
                prepare.SourceNodeRid,
                CancellationToken.None);
        }
        else if (expectedSuccess)
            await recovered.CompleteAsync(
                complete,
                prepare.SourceNodeRid,
                CancellationToken.None);
        else
            await Assert.ThrowsAnyAsync<Exception>(
                () => recovered.CompleteAsync(
                        complete,
                        prepare.SourceNodeRid,
                        CancellationToken.None)
                    .AsTask());
        await Assert.ThrowsAnyAsync<Exception>(
            () => recovered.CompleteAsync(
                    complete with { SourceCleanupState = 2 },
                    prepare.SourceNodeRid,
                    CancellationToken.None)
                .AsTask());
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
        ZLinkLocationOwnerToken SourceOwner,
        RoutingId TargetRid,
        ZLinkLocationOwnerToken TargetOwner,
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
            return new Fixture(
                store,
                permits,
                owner,
                time,
                sourceRid,
                sourceLease.Token,
                targetRid,
                targetLease.Token,
                coordinatorLease.Token,
                key,
                prepare);
        }

        internal ZLinkServiceWireCodec.RelocationReadyRecord Acceptance(
            ZLinkServiceWireCodec.RelocationReadyRecord offer,
            ZLinkServiceWireCodec.RelocationPrepareRecord? prepare = null)
            => offer with
        {
            Role = 1,
            OfferedMessages = 0,
            OfferedBytes = 0,
            Participants = (prepare ?? Prepare).Participants
        };

        internal async Task<ZLinkServiceWireCodec.RelocationPrepareRecord>
            CreateInstancePrepareAsync(
                bool staleNode = false,
                bool staleOwner = false)
        {
            var spotId = $"instance-{Guid.NewGuid():N}";
            const string stableType = "Game.Instance";
            var key = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(spotId);
            var sourceDescriptor = new ZLinkMeshNodeDescriptorKey(
                "mesh",
                SourceRid);
            var creation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
                await Store.ReserveAsync(new ZLinkObjectReservationRequest(
                    ZLinkPlacementObjectKind.InstanceSpot,
                    key,
                    stableType,
                    $"intent-{spotId}",
                    SHA256.HashData([3]),
                    1,
                    sourceDescriptor,
                    1,
                    SourceOwner,
                    new byte[] { 3 },
                    new ZLinkCapacityVector(
                        0,
                        1,
                        new ZLinkSpotTypeCapacityDelta(
                            ZLinkPlacementObjectKind.InstanceSpot,
                            stableType,
                            1)))));
            var authority = new ZLinkInstanceSpotAuthorityPayload(
                ZLinkInstanceSpotAuthorityState.Ready,
                spotId,
                stableType,
                "mesh",
                staleNode ? RoutingId.From("stale-source") : SourceRid,
                1,
                staleOwner ? "stale-owner" : SourceOwner.OwnerId,
                checked((ulong)SourceOwner.LeaseGeneration),
                RecoveryReference: null,
                RecoveryChecksum: 0,
                ReplayCursor: 0);
            var ready = Assert.IsType<ZLinkObjectCommitResult.Committed>(
                await Store.CommitAsync(
                    creation.Reservation,
                    ZLinkInstanceSpotAuthorityPayloadCodec.Encode(authority)));
            var relocationId = Guid.NewGuid();
            var participant =
                new ZLinkServiceWireCodec.RelocationParticipantRecord(
                    1,
                    1,
                    default,
                    0,
                    null,
                    0,
                    default,
                    0,
                    0,
                    0);
            return new ZLinkServiceWireCodec.RelocationPrepareRecord(
                new ZLinkServiceWireCodec.RelocationWireId(
                    BinaryPrimitives.ReadUInt64BigEndian(
                        relocationId.ToByteArray(bigEndian: true)
                            .AsSpan(0, 8)),
                    BinaryPrimitives.ReadUInt64BigEndian(
                        relocationId.ToByteArray(bigEndian: true)
                            .AsSpan(8, 8))),
                1,
                1,
                new ZLinkServiceWireCodec.RelocationCoordinatorFence(
                    CoordinatorOwner.OwnerId,
                    checked((ulong)CoordinatorOwner.LeaseGeneration),
                    RoutingId.From("reservation-coordinator"),
                    1,
                    ready.Snapshot.StoreVersion),
                new ZLinkServiceWireCodec.RelocationCandidateRecord(
                    TargetRid,
                    1,
                    TargetOwner.OwnerId,
                    checked((ulong)TargetOwner.LeaseGeneration)),
                1,
                new ZLinkServiceWireCodec.RelocationObjectRecord(
                    3,
                    stableType,
                    spotId,
                    ready.Snapshot.ObjectGeneration,
                    ExpectedAuthorityOwnerGeneration: 0),
                SourceRid,
                1,
                0,
                0,
                [participant],
                new ZLinkServiceWireCodec.RelocationRootRecord(
                    "instance-root",
                    3),
                1);
        }

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
                    false, 0),
                new ZLinkObjectCapability(
                    ZLinkPlacementObjectKind.InstanceSpot,
                    "Game.Instance",
                    ZLinkObjectMaintenancePolicyKind.Recreate,
                    false,
                    100)
            ],
            State = ZLinkFrameworkRuntimeState.Serving,
            EntrySpotId = $"{rid.ToHex()}-entry-00000000-0000-4000-8000-000000000001",
            Capacity = new ZLinkPlacementCapacity(
                new ZLinkPopulationCapacity(0, 0, 100),
                new ZLinkPopulationCapacity(0, 0, 100),
                [
                    new ZLinkSpotTypeCapacity(
                        ZLinkPlacementObjectKind.InstanceSpot,
                        "Game.Instance",
                        0,
                        0,
                        100)
                ])
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
        internal int AggregatePrepareCalls { get; private set; }

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
            CancellationToken cancellationToken = default)
        {
            AggregatePrepareCalls++;
            return inner.PrepareAggregateAsync(request, cancellationToken);
        }

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
        private readonly bool _loseFirstCompletionAck;
        private readonly ulong _allowanceMessages;
        private readonly Task _sealCompletion;
        private readonly RoutingId _targetRid;
        private readonly bool _rejectOffer;

        internal RawAttemptTarget(RoutingId sourceRid,
            ulong sourceGeneration, RoutingId targetRid,
            ulong targetGeneration,
            bool loseFirstCompletionAck = false,
            byte objectKind = 1,
            ulong allowanceMessages = 1,
            Task? sealCompletion = null,
            bool rejectOffer = false)
        {
            _loseFirstCompletionAck = loseFirstCompletionAck;
            _allowanceMessages = allowanceMessages;
            _sealCompletion = sealCompletion ?? Task.CompletedTask;
            _targetRid = targetRid;
            _rejectOffer = rejectOffer;
            var participant =
                new ZLinkServiceWireCodec.RelocationParticipantRecord(
                    1, 1, default, 0, null, 0, default, 0,
                    allowanceMessages,
                    allowanceMessages == 0 ? 0UL : 256UL);
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
                    objectKind,
                    objectKind == 3 ? "Game.Instance" : string.Empty,
                    objectKind == 1 ? "actor-1" : "spot-1",
                    1, objectKind == 3 ? 0UL : 1UL),
                sourceRid, sourceGeneration,
                allowanceMessages,
                allowanceMessages == 0 ? 64UL : 256UL,
                [participant],
                new ZLinkServiceWireCodec.RelocationRootRecord("root", 1), 1);
        }

        internal ZLinkServiceWireCodec.RelocationPrepareRecord Prepare { get; }
        internal int Offers { get; private set; }
        internal int DataRecords { get; private set; }
        internal int SealResponses { get; private set; }
        internal int Completions { get; private set; }

        public ValueTask<ZLinkServiceWireCodec.RelocationReadyRecord> OfferAsync(
            ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken)
        {
            if (authenticatedSourceNodeRid != prepare.SourceNodeRid
                || prepare.Candidate.NodeRid != _targetRid)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ProtocolError,
                    "Command 40 authenticated route does not match.");
            if (_rejectOffer)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.Unavailable,
                    "The target has no inbound relocation capacity.",
                    ZLinkRetryAdvice.RetryAfterBackoff);
            Offers++;
            return ValueTask.FromResult(
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
        }

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
            if (_sealRequested
                || checked((ulong)DataRecords) != _allowanceMessages)
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

        public async ValueTask AcceptSealResponseAsync(
            ZLinkServiceWireCodec.RelocationSealRecord seal,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken)
        {
            SealResponses++;
            await _sealCompletion.WaitAsync(cancellationToken)
                .ConfigureAwait(false);
        }

        public ValueTask CompleteAsync(
            ZLinkServiceWireCodec.RelocationCompleteRecord complete,
            RoutingId authenticatedSourceNodeRid,
            CancellationToken cancellationToken)
        {
            Completions++;
            if (_loseFirstCompletionAck && Completions == 1)
                throw new IOException(
                    "Simulated loss before the target completion ACK.");
            return ValueTask.CompletedTask;
        }
    }

    private sealed class DurableReceiptStore(
        TimeProvider? timeProvider = null) : IZLinkRelocationRepository
    {
        private readonly TimeProvider _time =
            timeProvider ?? TimeProvider.System;
        private readonly Dictionary<string, StoredReceipt> _values =
            new(StringComparer.Ordinal);

        internal string? FailNextPutReference { get; set; }

        public ValueTask<ZLinkRelocationStored> PutRelocationAsync(
            ReadOnlyMemory<byte> payload,
            TimeSpan retention,
            CancellationToken cancellationToken = default) =>
            PutRelocationAtAsync(
                Guid.NewGuid().ToString("N"),
                payload,
                retention,
                cancellationToken);

        public ValueTask<ZLinkRelocationStored> PutRelocationAtAsync(
            string reference,
            ReadOnlyMemory<byte> payload,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (StringComparer.Ordinal.Equals(
                    reference,
                    FailNextPutReference))
            {
                FailNextPutReference = null;
                throw new IOException(
                    $"Simulated durable write failure for '{reference}'.");
            }
            if (_values.TryGetValue(reference, out var existing)
                && existing.ExpiresAt > _time.GetUtcNow()
                && !existing.Payload.AsSpan().SequenceEqual(payload.Span))
                throw new InvalidOperationException(
                    $"Relocation reference '{reference}' changed.");
            var now = _time.GetUtcNow();
            _values[reference] = new StoredReceipt(
                payload.ToArray(),
                now + retention);
            return ValueTask.FromResult(
                new ZLinkRelocationStored(
                    reference,
                    ZLinkCrc32C.Compute(payload.Span),
                    now + retention,
                    now));
        }

        public ValueTask<ZLinkRelocationReadResult> GetRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!_values.TryGetValue(reference, out var stored)
                || stored.ExpiresAt <= _time.GetUtcNow())
            {
                _values.Remove(reference);
                return ValueTask.FromResult<ZLinkRelocationReadResult>(
                    new ZLinkRelocationReadResult.Missing());
            }
            return ValueTask.FromResult<ZLinkRelocationReadResult>(
                new ZLinkRelocationReadResult.Found(stored.Payload));
        }

        public ValueTask<ZLinkRelocationRenewResult> RenewRelocationAsync(
            string reference,
            TimeSpan retention,
            CancellationToken cancellationToken = default)
        {
            var now = _time.GetUtcNow();
            if (!_values.TryGetValue(reference, out var stored)
                || stored.ExpiresAt <= now)
            {
                _values.Remove(reference);
                return ValueTask.FromResult<ZLinkRelocationRenewResult>(
                    new ZLinkRelocationRenewResult.Missing());
            }
            _values[reference] = stored with { ExpiresAt = now + retention };
            return ValueTask.FromResult<ZLinkRelocationRenewResult>(
                new ZLinkRelocationRenewResult.Renewed(
                    now + retention,
                    now));
        }

        public ValueTask<ZLinkRelocationDeleteResult> DeleteRelocationAsync(
            string reference,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(
                _values.Remove(reference)
                    ? ZLinkRelocationDeleteResult.Deleted
                    : ZLinkRelocationDeleteResult.Missing);

        private sealed record StoredReceipt(
            byte[] Payload,
            DateTimeOffset ExpiresAt);
    }

    private sealed class DurableAuthorityStore(
        ZLinkAuthoritySnapshot snapshot) : ZLinkLocationStoreTestDouble
    {
        public override ValueTask<ZLinkAuthorityReadResult>
            ReadAuthorityAsync(
                ZLinkAuthorityKey key,
                CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkAuthorityReadResult>(
                new ZLinkAuthorityReadResult.Found(snapshot));
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
