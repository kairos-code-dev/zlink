using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Actors;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class EnvelopeCodecTests
{
    [Fact]
    public void Envelope_requires_marker_and_roundtrips_flow_fields()
    {
        var flowId = "0196f7c2-4cb4-7cc8-89d4-2d6aee6fca2d";
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            "play",
            "Move",
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null)
        {
            FlowId = flowId,
            FlowOrigin = ZLinkFlowOrigin.Application
        };

        using var encoded = ZLinkEnvelopeCodec.EncodeHeader(header);
        var decoded = ZLinkEnvelopeCodec.DecodeHeader(encoded);

        Assert.Equal(0xF2, decoded.FormatMarker);
        Assert.Equal(flowId, decoded.FlowId);
        Assert.Equal(ZLinkFlowOrigin.Application, decoded.FlowOrigin);

        using var missingMarker = Message.From(
            "{\"Kind\":3,\"ChannelName\":\"play\",\"MessageName\":\"Move\",\"ContentType\":\"application/json\"}");
        Assert.Throws<InvalidOperationException>(() => ZLinkEnvelopeCodec.DecodeHeader(missingMarker));
    }

    [Fact]
    public void DecodeBody_Returns_Message_When_BodyType_Is_Message()
    {
        using var body = Message.From("raw-join-request");

        var decoded = ZLinkEnvelopeCodec.DecodeBody(
            body,
            typeof(Message),
            ZLinkEnvelopeCodec.DefaultContentType,
            null);

        Assert.Same(body, decoded);
    }

    [Fact]
    public void EncodeBody_Copies_Message_When_BodyType_Is_Message()
    {
        using var body = Message.From("raw-join-reply");
        using var encoded = ZLinkEnvelopeCodec.EncodeBody(body, typeof(Message), null);

        Assert.NotSame(body, encoded);
        Assert.Equal(body.ToArray(), encoded.ToArray());
    }

    [Fact]
    public void JsonEnvelope_RoundTrips_ActorRefSnapshot_RoutingId_As_Hex()
    {
        var snapshot = new ActorRefSnapshot(
            RoutingId.From("delivery-courier-node-1"),
            "courier-a",
            3);
        using var body = ZLinkEnvelopeCodec.EncodeBody(
            new ActorSnapshotReply(snapshot),
            typeof(ActorSnapshotReply),
            null);

        var decoded = (ActorSnapshotReply)ZLinkEnvelopeCodec.DecodeBody(
            body,
            typeof(ActorSnapshotReply),
            ZLinkEnvelopeCodec.DefaultContentType,
            null)!;

        Assert.Equal(snapshot.NodeRid, decoded.Actor.NodeRid);
        Assert.Equal("delivery-courier-node-1", decoded.Actor.NodeRid.ToString());
        Assert.Equal(snapshot.ToActorRef(), decoded.Actor.ToActorRef());
    }

    [Fact]
    public void BoundSessionBindPacketName_Can_Be_Encoded_As_Stream_Send()
    {
        Assert.False(
            ZLinkRemoteActorJoinPackets.BoundSessionBindPacketName.StartsWith("__zlink.", StringComparison.Ordinal));

        var encoded = ZLinkStreamHeaderCodec.Encode(
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                null,
                ZLinkRemoteActorJoinPackets.BoundSessionBindPacketName,
                ZlinkStreamMetadata.Empty));

        Assert.False(encoded.IsEmpty);
    }

    [Theory]
    [InlineData(nameof(OperationCanceledException), typeof(OperationCanceledException))]
    [InlineData(nameof(TaskCanceledException), typeof(TaskCanceledException))]
    public void DecodeEnvelopeReply_Restores_Cancellation_Error(
        string errorCode,
        Type expectedExceptionType)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Error,
            "yield.route",
            "YieldReq",
            ZLinkEnvelopeCodec.DefaultContentType,
            "cancelled",
            null,
            null,
            errorCode,
            "A task was canceled.");
        var parts = ZLinkEnvelopeCodec.EncodeParts(header, null, null, null);

        var exception = Assert.ThrowsAny<OperationCanceledException>(
            () => ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<object>(
                parts,
                "empty",
                "failed",
                null));

        Assert.IsType(expectedExceptionType, exception);
        Assert.Equal("A task was canceled.", exception.Message);
    }

    private sealed record ActorSnapshotReply(ActorRefSnapshot Actor);
}
