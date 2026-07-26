using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Runtime.Protocol;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Service;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class CanonicalActorAcceptedJournalTests
{
    [Fact]
    public void Request_round_trip_preserves_operation_reply_and_authority_fences()
    {
        var source = SourceFence("source-owner", 41);
        var target = new ZLinkBackendActorRef(
            RoutingId.From("target-node"), "actor-1", 17);
        var header = ZLinkStreamProtocolDefaults.EncodeHeader(
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Request,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.None,
                new ZlinkStreamRequestSeq(71),
                "game.lookup",
                ZlinkStreamMetadata.Empty,
                CorrelationId: "correlation-1"));
        var frame = new ZLinkActorHandoffFrame(
            RoutingId.From("reply-node").ToBytes().ToArray(),
            19,
            source.NodeRid.ToBytes().ToArray(),
            RoutingId.From("session-1").ToBytes().ToArray(),
            73,
            1,
            header.ToArray(),
            [1, 2, 3, 4],
            9,
            new ZLinkBackendActorRouteContext(
                new MeshOperationId(0x1122334455667788, 0x99aabbccddeeff00),
                2,
                23,
                29,
                31,
                ReplyRequestId: 73,
                ReplyFlags: 5,
                ReplyCapability: "actor-reply"));

        var encoded = ZLinkCanonicalActorAcceptedJournal.Encode(
            frame, source, target);
        var decoded = ZLinkCanonicalActorAcceptedJournal.Decode(
            encoded, frame.ArrivalIndex, source);

        Assert.True(ZLinkRelocationEnvelopeCodec
            .TryValidateCanonicalFrozenRecord(encoded));
        Assert.Equal(source, decoded.Source);
        Assert.Equal(target, decoded.TargetActor);
        Assert.Equal(frame.RouteContext.OperationId,
            decoded.Frame.RouteContext.OperationId);
        Assert.Equal<ulong>(73, decoded.Frame.RouteContext.ReplyRequestId);
        Assert.Equal<ulong>(23,
            decoded.Frame.RouteContext.TargetNodeGeneration);
        Assert.Equal<ulong>(29,
            decoded.Frame.RouteContext.AuthorityOwnerGeneration);
        Assert.Equal<ulong>(31,
            decoded.Frame.RouteContext.OwnerLeaseGeneration);
        Assert.Equal(frame.Header, decoded.Frame.Header);
        Assert.Equal(frame.Body, decoded.Frame.Body);
        Assert.Equal(frame.SourceSessionRid, decoded.Frame.SourceSessionRid);
        Assert.Equal(frame.ReplyActorNodeRid,
            decoded.Frame.ReplyActorNodeRid);
    }

    [Fact]
    public void Decode_rejects_request_source_mismatch()
    {
        var source = SourceFence("source-owner", 41);
        var frame = Frame(source);
        var encoded = ZLinkCanonicalActorAcceptedJournal.Encode(
            frame,
            source,
            new ZLinkBackendActorRef(
                RoutingId.From("target-node"), "actor-1", 17));

        Assert.Throws<InvalidDataException>(() =>
            ZLinkCanonicalActorAcceptedJournal.Decode(
                encoded,
                1,
                SourceFence("replacement-owner", 42)));
    }

    [Fact]
    public void Encode_rejects_missing_source_fence()
    {
        var source = SourceFence("source-owner", 41);
        var frame = Frame(source);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            ZLinkCanonicalActorAcceptedJournal.Encode(
                frame,
                default,
                new ZLinkBackendActorRef(
                    RoutingId.From("target-node"), "actor-1", 17)));
    }

    private static ZLinkActorHandoffFrame Frame(
        ZLinkServiceWireCodec.RequestSourceFence source)
    {
        var header = ZLinkStreamProtocolDefaults.EncodeHeader(
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Request,
                ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                new ZlinkStreamRequestSeq(1),
                "packet",
                ZlinkStreamMetadata.Empty));
        return new ZLinkActorHandoffFrame(
            [], 0, source.NodeRid.ToBytes().ToArray(), [],
            7, 1, header.ToArray(), [1], 1,
            new ZLinkBackendActorRouteContext(
                new MeshOperationId(1, 2), 0, 3, 4, 5,
                ReplyRequestId: 7));
    }

    private static ZLinkServiceWireCodec.RequestSourceFence SourceFence(
        string ownerId,
        ulong lease) => new(
        ownerId,
        lease,
        RoutingId.From("source-node"),
        37);
}
