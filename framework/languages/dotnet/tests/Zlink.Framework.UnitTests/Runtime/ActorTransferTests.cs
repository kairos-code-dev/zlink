using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Codecs;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class ActorTransferTests
{
    [Fact]
    public async Task TransferRegistry_Invokes_CustomAdapter_With_TargetActorContext()
    {
        var services = new ServiceCollection()
            .AddSingleton<TransferActorAdapter>()
            .BuildServiceProvider();
        var transfer = new ZLinkActorTransferRegistration(
            typeof(TransferActor),
            typeof(TransferActorAdapter));
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
        public RoutingId? SpotRid => null;

        public bool IsJoined => false;

        public IZLinkBoundSession BoundSession => throw new NotSupportedException();

        public IZLinkSpot GetSpot()
        {
            throw new NotSupportedException();
        }

        public TSpot GetSpot<TSpot>()
            where TSpot : IZLinkSpot
        {
            throw new NotSupportedException();
        }

        public IZLinkActorJoinSpotCall JoinSpot(
            RoutingId spotRid,
            ZLinkMessage request)
        {
            throw new NotSupportedException();
        }

        public IZLinkActorJoinEntrySpotCall JoinEntrySpot(
            RoutingId spotNodeRid,
            ZLinkMessage request)
        {
            throw new NotSupportedException();
        }
    }
}
