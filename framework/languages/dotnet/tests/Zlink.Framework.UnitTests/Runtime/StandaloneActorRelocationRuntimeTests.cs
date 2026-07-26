using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class StandaloneActorRelocationRuntimeTests
{
    [Fact]
    public void Canonical_target_import_is_idempotent_and_replays_fifo()
    {
        var handoff = new ZLinkActorHandoffState(
            "actor-1",
            TimeProvider.System);
        var frames = new[]
        {
            AcceptedFrame(2),
            AcceptedFrame(1)
        };

        handoff.BeginCanonicalMaintenanceImport("handoff", frames);
        handoff.BeginCanonicalMaintenanceImport("handoff", frames);
        handoff.MarkAuthorityCommitted("handoff", 42, 42);
        var replay = handoff.PrepareCanonicalMaintenanceReplay("handoff");

        Assert.Equal(new ulong[] { 1, 2 },
            replay.Select(static frame => frame.RequestId));
        handoff.AcknowledgeReplayedFrame();
        handoff.AcknowledgeReplayedFrame();
        handoff.Complete("handoff");
    }

    [Fact]
    public void Canonical_target_rejects_object_generation_change()
    {
        var handoff = new ZLinkActorHandoffState(
            "actor-1",
            TimeProvider.System);
        handoff.BeginCanonicalMaintenanceImport("handoff", []);

        var error = Assert.Throws<ZLinkFrameworkException>(() =>
            handoff.MarkAuthorityCommitted("handoff", 42, 43));

        Assert.Equal(ZLinkFrameworkErrorKind.ActorGenerationStale, error.Kind);
    }

    [Fact]
    public void Startup_recovery_routes_canonical_actor_root_to_standalone_owner()
    {
        var source = SourceAuthority();
        var actor = SourceActorAuthority();
        var target = TargetDescriptor();
        var root = ZLinkStandaloneActorRelocationRuntime.CreateImmutableRoot(
            source,
            actor,
            target,
            Guid.NewGuid(),
            ReadOnlyMemory<byte>.Empty,
            [],
            default);
        var candidate = new ZLinkRelocationRecoveryCandidate(
            new ZLinkRelocationManifestReference(
                "relocation-root",
                1,
                root.AggregateId,
                root.AggregateGeneration,
                root.InventoryDigest),
            root,
            [
                new ZLinkAuthorityEntry(
                    root.Participants[0].AuthorityKey,
                    source)
            ]);

        Assert.True(ZLinkStandaloneActorRelocationRuntime.OwnsRecovery(
            candidate));
        Assert.False(ZLinkStandaloneActorRelocationRuntime.OwnsRecovery(
            candidate with
            {
                Envelope = root with
                {
                    Participants =
                    [root.Participants[0] with { RecoveryPayload = "{}"u8.ToArray() }]
                }
            }));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Immutable_root_preserves_generation_and_orders_accepted_queue(
        bool snapshot)
    {
        var source = SourceAuthority();
        var authority = SourceActorAuthority();
        var target = TargetDescriptor();
        var targetRid = target.Rid;
        var frames = new[]
        {
            AcceptedFrame(2),
            AcceptedFrame(1)
        };

        var root = ZLinkStandaloneActorRelocationRuntime.CreateImmutableRoot(
            source,
            authority,
            target,
            Guid.NewGuid(),
            snapshot ? new byte[] { 9, 8, 7 } : [],
            frames,
            default);

        var participant = Assert.Single(root.Participants);
        Assert.Equal(42UL, participant.ObjectGeneration);
        Assert.Equal(11UL, participant.AuthorityOwnerGeneration);
        Assert.Equal(1UL, participant.CanonicalParticipantId);
        Assert.Equal(new ulong[] { 1, 2 },
            participant.AcceptedJobs.Select(static job => job.AcceptedSequence));
        Assert.Equal(snapshot ? 3 : 0, participant.ApplicationState.Length);
        var recovery = ZLinkCanonicalParticipantRecoveryCodec.Decode(
            participant.RecoveryPayload.Span);
        Assert.Equal("v9", recovery.ExpectedStoreVersion);
        Assert.True(ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
            recovery.AuthorityPayload.Span,
            out var relocating));
        Assert.True(ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
            relocating.ApplicationPayload.Span,
            out var targetAuthority));
        Assert.Equal(42UL, recovery.ObjectGeneration);
        Assert.Equal(11UL, recovery.AuthorityOwnerGeneration);
        Assert.Equal(targetRid, targetAuthority.NodeRid);
        Assert.Equal("target-entry", targetAuthority.CurrentSpotId);
        Assert.Equal("target-owner", targetAuthority.OwnerId);
        Assert.Equal(4UL, targetAuthority.OwnerLeaseGeneration);
        var sourceFence = ZLinkActorRelocationSourceFenceCodec.Decode(
            recovery.MembershipMutation.Span);
        Assert.Equal("source-owner", sourceFence.OwnerId);
        Assert.Equal(3UL, sourceFence.OwnerLeaseGeneration);
        Assert.Equal(RoutingId.From("source"), sourceFence.NodeRid);
        Assert.Equal(7UL, sourceFence.NodeGeneration);
    }

    [Fact]
    public void Startup_recovery_requires_exact_source_lease_expiry()
    {
        var now = DateTimeOffset.UtcNow;
        var source = new ZLinkActorRelocationSourceFence(
            "source-owner", 3, RoutingId.From("source"), 7);

        Assert.True(ZLinkStandaloneActorRelocationRuntime
            .IsExactSourceLeaseExpired(
                new ZLinkOwnerLeaseReadResult.Missing(), source));
        Assert.True(ZLinkStandaloneActorRelocationRuntime
            .IsExactSourceLeaseExpired(
                new ZLinkOwnerLeaseReadResult.Found(
                    new ZLinkLocationOwnerToken("source-owner", 4),
                    now.AddMinutes(1), now), source));
        Assert.True(ZLinkStandaloneActorRelocationRuntime
            .IsExactSourceLeaseExpired(
                new ZLinkOwnerLeaseReadResult.Found(
                    new ZLinkLocationOwnerToken("source-owner", 3),
                    now, now), source));
        Assert.False(ZLinkStandaloneActorRelocationRuntime
            .IsExactSourceLeaseExpired(
                new ZLinkOwnerLeaseReadResult.Found(
                    new ZLinkLocationOwnerToken("source-owner", 3),
                    now.AddMinutes(1), now), source));
    }

    private static ZLinkAuthoritySnapshot SourceAuthority()
    {
        var sourceRid = RoutingId.From("source");
        return new ZLinkAuthoritySnapshot(
            "v9",
            ReadOnlyMemory<byte>.Empty,
            42,
            11,
            "source-owner",
            3,
            new ZLinkPlacementAllocation(
                ZLinkPlacementAllocationState.Active,
                ZLinkPlacementObjectKind.Actor,
                "player",
                new ZLinkMeshNodeDescriptorKey("mesh", sourceRid),
                7,
                new ZLinkCapacityVector(1, 0, null)),
            null,
            DateTimeOffset.UtcNow);
    }

    private static ZLinkActorAuthorityPayload SourceActorAuthority() => new(
        ZLinkActorAuthorityState.Ready,
        "player",
        "actor-1",
        "source-entry",
        7,
        ZLinkSpotKind.Entry,
        "source-owner",
        3,
        "mesh",
        RoutingId.From("source"),
        7);

    private static ZLinkMeshNodeDescriptor TargetDescriptor() => new(
        "mesh",
        RoutingId.From("target"),
        8,
        1,
        "tcp://127.0.0.1:1",
        new Dictionary<string, int>(),
        "plain",
        "target-owner",
        4,
        DateTimeOffset.UtcNow)
    {
        EntrySpotId = "target-entry"
    };

    private static ZLinkActorHandoffFrame AcceptedFrame(long arrivalIndex)
    {
        var header = ZLinkStreamProtocolDefaults.EncodeHeader(
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                default,
                "packet",
                ZlinkStreamMetadata.Empty));
        return new ZLinkActorHandoffFrame(
            [],
            0,
            RoutingId.From("source").ToBytes().ToArray(),
            [],
            checked((ulong)arrivalIndex),
            0,
            header.ToArray(),
            [checked((byte)arrivalIndex)],
            arrivalIndex,
            new ZLinkBackendActorRouteContext(
                new MeshOperationId(1, checked((ulong)arrivalIndex)),
                0,
                7,
                11,
                3));
    }
}
