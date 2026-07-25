using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class ActorTransferTests
{
    [Fact]
    public async Task Transfer_admission_commit_and_target_continuation_keep_one_root_flow()
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
                codecs);
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
                ZLinkMessage.From("state"),
                ZLinkMessage.From("join"),
                [],
                codecs);
        }

        Assert.Null(ZLinkFlowContext.Current);
        ZLinkEnvelopeHeader? targetContinuation = null;
        var targetIngress = new List<(string Packet, ZLinkFlowValue Flow)>();
        var options = new ZLinkDispatchOptionsModel();
        options.MessageFlow(ZLinkMessageFlowLogMode.Off);
        var dispatcher = new ZLinkSpotRouteDispatcher(
            "actor-route",
            "target-spot",
            new ZLinkSpotPacketRegistry(),
            static () => throw new InvalidOperationException("Only internal transfer packets are expected."),
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
    public async Task TransferRegistry_Invokes_CustomAdapter_With_TargetActorContext()
    {
        var services = new ServiceCollection()
            .AddSingleton<TransferActorAdapter>()
            .BuildServiceProvider();
        var transfer = ZLinkActorTransferRegistry
            .CreateRegistration<TransferActor, TransferActorAdapter>();
        var source = new TransferActor("actor-1", new TestActorContext(), "source-state");
        var state = await ZLinkActorTransferRegistry.TransferOutAsync(
            services,
            transfer,
            source,
            CancellationToken.None);
        var targetContext = new TestActorContext();

        var target = (TransferActor)await ZLinkActorTransferRegistry.TransferInAsync(
            services,
            transfer,
            "actor-1",
            targetContext,
            state,
            CancellationToken.None);

        Assert.Equal("source-state", state.Decode<string>());
        Assert.Equal("actor-1", target.ActorId);
        Assert.Equal("source-state", target.State);
        Assert.Same(targetContext, target.Context);
    }

    [Fact]
    public void RemoteJoinPacket_Carries_TransferState_Separately_From_JoinRequest()
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
            ZLinkMessage.From("transfer-state"),
            ZLinkMessage.From("join-request"),
            [],
            codecs);

        var decoded = ZLinkRemoteActorJoinPackets.DecodeJoinRequest(parts);

        Assert.Equal("transfer-state", ZLinkRemoteActorJoinPackets.DecodeTransferState(decoded, codecs).Decode<string>());
        Assert.Equal("join-request", ZLinkRemoteActorJoinPackets.DecodeJoinRequestPayload(decoded, codecs).Decode<string>());
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

    // RouteMesh 10.0.0 hands the spot route dispatcher a framework-owned
    // ZLinkBackendRouteReceived (drained from Core claims by the node pump) rather
    // than a binding Received. The record owns a private copy of the parts and is
    // disposed by the dispatcher, so the caller's originals remain valid.
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

    private sealed class TransferActorAdapter : IZLinkActorTransferAdapter<TransferActor>
    {
        public ValueTask<ZLinkMessage> TransferOutAsync(
            TransferActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.FromResult(ZLinkMessage.From(actor.State));
        }

        public ValueTask<TransferActor> TransferInAsync(
            string actorId,
            IZLinkActorContext context,
            ZLinkMessage state,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.FromResult(new TransferActor(actorId, context, state.Decode<string>()));
        }
    }

    private sealed class TransferActor(
        string actorId,
        IZLinkActorContext context,
        string state) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public string State { get; } = state;
    }

    private sealed class TestActorContext : IZLinkActorContext
    {
        public string ActorId => "actor-1";

        public ulong ObjectGeneration => 1;

        public string MeshName => "play";

        public string? SpotId => null;

        public IZLinkBoundSession BoundSession => throw new NotSupportedException();

        public IZLinkActorJoinSpotCall JoinSpot(
            string spotId,
            ZLinkMessage request)
        {
            throw new NotSupportedException();
        }

        public IZLinkActorJoinEntrySpotCall JoinEntrySpot(
            ZLinkMessage request)
        {
            throw new NotSupportedException();
        }
    }
}
