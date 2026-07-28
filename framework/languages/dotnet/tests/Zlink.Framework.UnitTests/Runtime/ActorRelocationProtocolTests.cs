using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Dispatch;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class ActorRelocationProtocolTests
{
    [Fact]
    public void Startup_recovery_rejects_mismatched_enclosing_identity_before_callback()
    {
        var recovery = CreateRecovery([1], [2]);
        recovery = recovery with
        {
            Request = recovery.Request with
            {
                RelocationReference = "pending",
                RelocationChecksumCrc32c = 0,
                RelocationInventoryDigest = new byte[32]
            }
        };
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey("actor-1");
        var canonical = new ZLinkCanonicalParticipantRecovery(
            key,
            ZLinkPlacementObjectKind.Actor,
            7,
            3,
            "v1",
            "player",
            ReadOnlyMemory<byte>.Empty,
            ReadOnlyMemory<byte>.Empty);
        var participant = new ZLinkRelocationParticipantEnvelope(
            key,
            ZLinkPlacementObjectKind.Actor,
            7,
            3,
            ReadOnlyMemory<byte>.Empty,
            [],
            [],
            ZLinkCanonicalParticipantRecoveryCodec.Encode(canonical));
        var envelope = new ZLinkRelocationEnvelope(
            recovery.Request.RelocationAggregateId,
            recovery.Request.RelocationAggregateGeneration,
            new byte[32],
            [participant]);
        var candidate = new ZLinkRelocationRecoveryCandidate(
            new ZLinkRelocationManifestReference(
                "root-1",
                17,
                envelope.AggregateId,
                envelope.AggregateGeneration,
                envelope.InventoryDigest),
            envelope,
            []);

        ZLinkFrameworkRuntime.ValidateCanonicalRemoteJoinRecoveryIdentity(
            candidate,
            participant,
            canonical,
            recovery);

        var wrongDigest = new byte[32];
        wrongDigest[0] = 1;
        AssertDataLost(
            candidate with
            {
                Reference = candidate.Reference with
                {
                    InventoryDigest = wrongDigest
                }
            },
            participant,
            canonical,
            recovery);
        AssertDataLost(
            candidate,
            participant with { AuthorityOwnerGeneration = 4 },
            canonical,
            recovery);
        AssertDataLost(
            candidate,
            participant,
            canonical with { StableType = "mage" },
            recovery);
        AssertDataLost(
            candidate,
            participant,
            canonical,
            recovery with
            {
                Request = recovery.Request with
                {
                    RelocationReference = "substituted-root"
                }
            });

        static void AssertDataLost(
            ZLinkRelocationRecoveryCandidate candidate,
            ZLinkRelocationParticipantEnvelope participant,
            ZLinkCanonicalParticipantRecovery canonical,
            ZLinkActorRelocationRecoveryRecord recovery)
        {
            var error = Assert.Throws<ZLinkFrameworkException>(() =>
            ZLinkFrameworkRuntime.ValidateCanonicalRemoteJoinRecoveryIdentity(
                candidate,
                participant,
                canonical,
                recovery));

            Assert.Equal(ZLinkFrameworkErrorKind.DataLost, error.Kind);
        }
    }

    [Fact]
    public void Remote_join_recovery_preserves_independent_one_megabyte_messages()
    {
        const int maximumMessageBytes = 1024 * 1024;
        var requestPayload = Enumerable.Repeat((byte)0x5a, maximumMessageBytes)
            .ToArray();
        var replyPayload = Enumerable.Repeat((byte)0xa5, maximumMessageBytes)
            .ToArray();
        var recovery = CreateRecovery(requestPayload, replyPayload);
        var remoteJoinRecovery =
            ZLinkActorRemoteJoinRecoveryCodec.Encode(recovery);
        Assert.True(remoteJoinRecovery.Length > 2 * maximumMessageBytes);

        var sourceRid = RoutingId.From("source-node");
        var participantRecovery =
            ZLinkCanonicalParticipantRecoveryCodec.Encode(
                new ZLinkCanonicalParticipantRecovery(
                    new ZLinkAuthorityKey("actor:actor-1"),
                    ZLinkPlacementObjectKind.Actor,
                    7,
                    3,
                    "v1",
                    "player",
                    ReadOnlyMemory<byte>.Empty,
                    ZLinkActorRelocationSourceFenceCodec.Encode(
                        new ZLinkActorRelocationSourceFence(
                            "source-owner",
                            3,
                            sourceRid,
                            7)),
                    remoteJoinRecovery));
        var inventory = new ZLinkRelocationEnvelope(
            Guid.Parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"),
            1,
            new byte[32],
            [
                new ZLinkRelocationParticipantEnvelope(
                    new ZLinkAuthorityKey("actor:actor-1"),
                    ZLinkPlacementObjectKind.Actor,
                    7,
                    3,
                    ReadOnlyMemory<byte>.Empty,
                    [],
                    [],
                    participantRecovery)
                {
                    CanonicalParticipantId = 1
                }
            ]);
        var canonical =
            ZLinkCanonicalActorRelocationWriter.CreateInitial(inventory, 0);
        var restored = ZLinkRelocationEnvelopeCodec.Decode(
            ZLinkRelocationEnvelopeCodec.Encode(canonical));
        var restoredParticipant = Assert.Single(restored.Participants);
        var restoredPayload = ZLinkCanonicalParticipantRecoveryCodec.Decode(
            restoredParticipant.RecoveryPayload.Span).OperationRecovery;
        var restoredRecovery =
            ZLinkActorRemoteJoinRecoveryCodec.Decode(restoredPayload.Span);

        Assert.Equal(requestPayload, restoredRecovery.Request.Request);
        Assert.Equal(replyPayload, restoredRecovery.Reply);
    }

    [Fact]
    public void Remote_join_uses_the_authoritative_current_spot_id()
    {
        const string entrySpotId =
            "actor-a-entry-01234567-89ab-4cde-8fab-0123456789ab";
        var authority = new ZLinkActorAuthorityPayload(
            ZLinkActorAuthorityState.Ready,
            "player",
            "actor-1",
            entrySpotId,
            7,
            ZLinkSpotKind.Entry,
            "owner-1",
            3,
            "mesh",
            RoutingId.From("actor-a"),
            7);

        var sourceSpotId = ZLinkActorRemoteJoiner.ResolveSourceSpotId(authority);

        Assert.Equal(entrySpotId, sourceSpotId);
    }

    [Fact]
    public async Task Relocation_admission_commit_and_target_continuation_keep_one_root_flow()
    {
        const string flowId = "0196f7c2-4cb4-7cc8-89d4-2d6aee6fca2d";
        var codecs = new ZLinkCodecRegistryBuilder();
        IReadOnlyList<Message> admissionParts;
        IReadOnlyList<Message> commitParts;
        using (ZLinkFlowContext.Enter(
                   flowId,
                   ZLinkFlowOrigin.Application,
                   createIfAbsent: false,
                   ZLinkFlowOrigin.Inbound))
        {
            admissionParts = ZLinkRemoteActorJoinPackets.EncodeAdmissionRequest(
                ZLinkClientCallCodec.CreateEnvelope(
                    ZLinkMessageKind.Request,
                    "actor-route",
                    ZLinkRemoteActorJoinPackets.AdmissionPacketName,
                    TimeSpan.FromSeconds(1)),
                "actor-1",
                "player",
                "handoff-1",
                DateTimeOffset.UtcNow.AddSeconds(1),
                "source-spot",
                 RoutingId.From("source-node"),
                 ZLinkMessage.From("admission"),
                 codecs,
                 actorGeneration: 1,
                 actorAuthorityOwnerGeneration: 1,
                 predictedPayloadBytes: 1024,
                 targetSpotGeneration: 1,
                 targetSpotAuthorityOwnerGeneration: 1);
            commitParts = ZLinkRemoteActorJoinPackets.EncodeJoinRequest(
                ZLinkClientCallCodec.CreateEnvelope(
                    ZLinkMessageKind.Request,
                    "actor-route",
                    ZLinkRemoteActorJoinPackets.CommitPacketName,
                    TimeSpan.FromSeconds(1)),
                "actor-1",
                "player",
                "handoff-1",
                "source-spot",
                RoutingId.From("source-node"),
                1,
                1,
                null,
                default,
                ZLinkRemoteActorJoinPackets.SnapshotRelocationContentType,
                Reference(),
                ZLinkMessage.From("join"),
                codecs);
        }

        Assert.Null(ZLinkFlowContext.Current);
        ZLinkEnvelopeHeader? targetContinuation = null;
        var targetIngress = new List<(string Packet, ZLinkFlowValue Flow)>();
        var options = new ZLinkDispatchOptionsModel();
        options.Diagnostics.SetLevel(ZLinkDiagnosticsLevel.Off);
        var dispatcher = new ZLinkSpotRouteDispatcher(
            "actor-route",
            "target-spot",
            new ZLinkSpotPacketRegistry(),
            static () => throw new InvalidOperationException("Only internal relocation packets are expected."),
            codecs,
            new ZLinkDispatchErrorReporter(options),
            (_, header, _) =>
            {
                var current = Assert.IsType<ZLinkFlowValue>(ZLinkFlowContext.Current);
                targetIngress.Add((header.MessageName, current));
                if (header.MessageName == ZLinkRemoteActorJoinPackets.CommitPacketName)
                {
                    using var encoded = ZLinkEnvelopeCodec.EncodeHeader(
                        ZLinkClientCallCodec.CreateEnvelope(
                            ZLinkMessageKind.Command,
                            "actor-route",
                            "target-continuation"));
                    targetContinuation = ZLinkEnvelopeCodec.DecodeHeader(encoded);
                }
                return ValueTask.FromResult(true);
            });

        try
        {
            await dispatcher.DispatchAsync(CreateRoutedReceived(admissionParts), CancellationToken.None);
            await dispatcher.DispatchAsync(CreateRoutedReceived(commitParts), CancellationToken.None);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(admissionParts);
            ZLinkMessageParts.DisposeAll(commitParts);
        }

        Assert.Equal(
            [ZLinkRemoteActorJoinPackets.AdmissionPacketName, ZLinkRemoteActorJoinPackets.CommitPacketName],
            targetIngress.Select(entry => entry.Packet));
        Assert.All(targetIngress, entry =>
        {
            Assert.Equal(flowId, entry.Flow.FlowId);
            Assert.Equal(ZLinkFlowOrigin.Application, entry.Flow.Origin);
        });
        Assert.Equal(flowId, targetContinuation?.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, targetContinuation?.FlowOrigin);
        Assert.Null(ZLinkFlowContext.Current);
    }

    [Fact]
    public void RemoteJoinPacket_Carries_RelocationReference_Separately_From_JoinRequest()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            "router",
            ZLinkRemoteActorJoinPackets.RequestPacketName,
            TimeSpan.FromSeconds(5));

        var parts = ZLinkRemoteActorJoinPackets.EncodeJoinRequest(
            header,
            "actor-1",
            "player",
            "handoff-1",
            "source-spot",
            RoutingId.From("source-node"),
            1,
            1,
            RoutingId.From("source-node"),
            RoutingId.From("session-1"),
            ZLinkRemoteActorJoinPackets.SnapshotRelocationContentType,
            Reference(),
            ZLinkMessage.From("join-request"),
            codecs);

        var decoded = ZLinkRemoteActorJoinPackets.DecodeJoinRequest(parts);

        Assert.Equal(
            ZLinkRemoteActorJoinPackets.SnapshotRelocationContentType,
            decoded.RelocationContentType);
        Assert.Equal("root-1", decoded.RelocationReference);
        Assert.Equal((uint)17, decoded.RelocationChecksumCrc32c);
        Assert.Equal(32, decoded.RelocationInventoryDigest.Length);
        Assert.Equal("join-request", ZLinkRemoteActorJoinPackets.DecodeJoinRequestPayload(decoded, codecs).Decode<string>());
    }

    private static ZLinkRelocationManifestReference Reference() =>
        new(
            "root-1",
            17,
            Guid.Parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"),
            1,
            new byte[32]);

    private static ZLinkActorRelocationRecoveryRecord CreateRecovery(
        byte[] requestPayload,
        byte[] replyPayload)
    {
        var aggregateId =
            Guid.Parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
        var targetRid = RoutingId.From("target-node").ToBytes().ToArray();
        return new ZLinkActorRelocationRecoveryRecord(
            new ZLinkRemoteActorJoinRequest(
                "actor-1",
                "player",
                aggregateId.ToString("N"),
                null,
                null,
                ZLinkRemoteActorJoinPackets.SnapshotRelocationContentType,
                "root-1",
                17,
                aggregateId,
                1,
                new byte[32],
                ZLinkEnvelopeCodec.DefaultContentType,
                requestPayload,
                [],
                "source-spot",
                RoutingId.From("source-node").ToBytes().ToArray(),
                7,
                3,
                ReservationToken: "reservation-1",
                ReservedPayloadBytes: requestPayload.Length,
                TargetNodeRid: targetRid,
                TargetNodeGeneration: 11,
                TargetSpotGeneration: 5,
                TargetAuthorityOwnerGeneration: 4,
                TargetSpotAuthorityOwnerGeneration: 2),
            "target-spot",
            targetRid,
            11,
            5,
            4,
            19,
            41,
            ZLinkEnvelopeCodec.DefaultContentType,
            replyPayload);
    }

    [Fact]
    public void Actor_relocation_root_rejects_a_substituted_target_node_fence()
    {
        var aggregateId =
            Guid.Parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
        var request = new ZLinkRemoteActorJoinRequest(
            "actor-1",
            "player",
            aggregateId.ToString("N"),
            RoutingId.From("session-node").ToBytes().ToArray(),
            RoutingId.From("session").ToBytes().ToArray(),
            ZLinkRemoteActorJoinPackets.SnapshotRelocationContentType,
            "root-1",
            17,
            aggregateId,
            3,
            new byte[32],
            ZLinkEnvelopeCodec.DefaultContentType,
            [1],
            [],
            "source-spot",
            RoutingId.From("source-node").ToBytes().ToArray(),
            7,
            3,
            ReservationToken: "reservation-1",
            ReservedPayloadBytes: 1024,
            TargetNodeRid: RoutingId.From("target-node").ToBytes().ToArray(),
            TargetNodeGeneration: 11,
            TargetSpotGeneration: 5,
            TargetAuthorityOwnerGeneration: 4,
            TargetSpotAuthorityOwnerGeneration: 2);
        var recovery = new ZLinkActorRelocationRecoveryRecord(
            request,
            "target-spot",
            RoutingId.From("target-node").ToBytes().ToArray(),
            11,
            5,
            4,
            0,
            0,
            null,
            []);
        var sourceRid = RoutingId.From("source-node");
        var source = new ZLinkAuthoritySnapshot(
            "v1",
            ReadOnlyMemory<byte>.Empty,
            7,
            3,
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
        var sourceAuthority = new ZLinkActorAuthorityPayload(
            ZLinkActorAuthorityState.Ready,
            "player",
            "actor-1",
            "source-spot",
            7,
            ZLinkSpotKind.Entry,
            "source-owner",
            3,
            "mesh",
            sourceRid,
            7);
        var destination = new ZLinkStandaloneActorRelocationDestination(
            "target-spot",
            5,
            ZLinkSpotKind.Entry,
            RoutingId.From("target-node"),
            11,
            "mesh",
            new ZLinkLocationOwnerToken("target-owner", 4));
        var envelope =
            ZLinkStandaloneActorRelocationRuntime.CreateImmutableRoot(
                source,
                sourceAuthority,
                destination,
                aggregateId,
                new byte[] { 2 },
                [],
                default,
                ZLinkActorRemoteJoinRecoveryCodec.Encode(
                    recovery));
        var wire = request with
        {
            RelocationAggregateGeneration = envelope.AggregateGeneration,
            RelocationInventoryDigest = envelope.InventoryDigest.ToArray(),
            TargetNodeRid =
                RoutingId.From("other-target-node").ToBytes().ToArray()
        };

        var error = Assert.Throws<ZLinkFrameworkException>(() =>
        {
            _ = ZLinkActorRelocationRoot.Load(wire, envelope);
        });

        Assert.Equal(ZLinkFrameworkErrorKind.DataLost, error.Kind);
        Assert.False(error.RetryAdvice != ZLinkRetryAdvice.DoNotRetry);
    }

    [Fact]
    public void Handoff_completion_envelope_preserves_the_current_flow()
    {
        using var flow = ZLinkFlowContext.Enter(null, null, true, ZLinkFlowOrigin.Lifecycle);
        var expected = Assert.IsType<ZLinkFlowValue>(ZLinkFlowContext.Current);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            "router",
            ZLinkRemoteActorJoinPackets.HandoffCompletionPacketName,
            TimeSpan.FromSeconds(5));
        var parts = ZLinkRemoteActorJoinPackets.EncodeHandoffCompletionRequest(
            header,
            "actor-1",
            "handoff-1",
            "source-spot",
            RoutingId.From("source-node"),
            "target-spot",
            new ZLinkActorJoinOperationId(11, 29),
            new ZLinkRemoteActorAdmissionReply(
                true,
                ZLinkEnvelopeCodec.DefaultContentType,
                [1, 2, 3],
                0),
            null,
            []);
        try
        {
            var decoded = ZLinkEnvelopeCodec.DecodeHeader(parts);
            Assert.Equal(expected.FlowId, decoded.FlowId);
            Assert.Equal(expected.Origin, decoded.FlowOrigin);
            var completion = ZLinkRemoteActorJoinPackets.DecodeHandoffCompletionRequest(parts);
            Assert.Equal((ulong)11, completion.OperationIdHigh);
            Assert.Equal((ulong)29, completion.OperationIdLow);
            Assert.Equal([1, 2, 3], completion.Reply);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    // The Framework route dispatcher receives a framework-owned record. It owns a
    // private copy of the parts and disposes that copy, so the caller's originals
    // remain valid.
    private static ZLinkBackendRouteReceived CreateRoutedReceived(IReadOnlyList<Message> parts)
    {
        var owned = parts
            .Select(static part => Message.From(part.AsReadOnlySpan()))
            .ToArray();
        return new ZLinkBackendRouteReceived(
            owned,
            sourceNodeRid: RoutingId.From("transfer-source"),
            spotId: "transfer-target",
            requestSeq: null,
            reply: null);
    }

}
