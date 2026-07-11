using System.Buffers.Binary;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Xunit;

public sealed partial class StreamConnectorTests
{
    [Fact]
    public void HeaderProtocolRoundTripsMetadataAndRequestSeq()
    {
        var codec = new ZlinkStreamHeaderCodec();
        var source = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            new ZlinkStreamRequestSeq(42),
            "profile.get",
            ZlinkStreamMetadata.Empty.With("traceId", "abc"));

        var decoded = codec.Decode(codec.Encode(source));

        Assert.Equal(source.Kind, decoded.Kind);
        Assert.Equal(source.Codec, decoded.Codec);
        Assert.Equal(source.RequestSeq, decoded.RequestSeq);
        Assert.Equal(source.Name, decoded.Name);
        Assert.Equal("abc", decoded.Metadata.Get("traceId"));
    }

    [Fact]
    public void HeaderProtocolRoundTripsEmptyMetadataValue()
    {
        var codec = new ZlinkStreamHeaderCodec();
        var source = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            "profile.update",
            ZlinkStreamMetadata.Empty.With("optional", ""));

        var decoded = codec.Decode(codec.Encode(source));

        Assert.Equal("", decoded.Metadata.Get("optional"));
    }

    // MFLOW-009: correlation id is a first-class header trailer (flag 0x08), wire layout
    // = after metadata, u8 length + UTF-8 bytes. Round-trips and is byte-exact.
    [Fact]
    public void HeaderProtocolRoundTripsCorrelationIdAfterMetadata()
    {
        var codec = new ZlinkStreamHeaderCodec();
        var source = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            new ZlinkStreamRequestSeq(7),
            "order.place",
            ZlinkStreamMetadata.Empty.With("k", "v"),
            "a1b2");

        var encoded = codec.Encode(source);
        var decoded = codec.Decode(encoded);

        Assert.Equal("a1b2", decoded.CorrelationId);
        Assert.True(decoded.Flags.HasFlag(ZlinkStreamHeaderFlags.HasCorrelationId));
        Assert.True(decoded.Flags.HasFlag(ZlinkStreamHeaderFlags.HasFlowId));
        Assert.True(ZlinkStreamFlowId.IsValid(decoded.FlowId));
        Assert.Equal(ZlinkStreamFlowOrigin.Application, decoded.FlowOrigin);

        var span = encoded.Span;
        Assert.Equal(ZlinkStreamFlowId.FormatMarker, span[0]);
        Assert.Equal((byte)4, span[^(ZlinkStreamFlowId.EncodedLength + 6)]);
        Assert.Equal(
            "a1b2",
            Encoding.UTF8.GetString(
                span.Slice(span.Length - ZlinkStreamFlowId.EncodedLength - 5, 4)));
    }

    [Fact]
    public void HeaderProtocolRoundTripsExplicitUuidV7AndRootOrigin()
    {
        var codec = new ZlinkStreamHeaderCodec();
        var flowId = ZlinkStreamFlowId.Create();
        var source = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            "flow.test",
            ZlinkStreamMetadata.Empty,
            FlowId: flowId,
            FlowOrigin: ZlinkStreamFlowOrigin.Timer);

        var decoded = codec.Decode(codec.Encode(source));

        Assert.Equal(flowId, decoded.FlowId);
        Assert.Equal(ZlinkStreamFlowOrigin.Timer, decoded.FlowOrigin);
    }

    [Fact]
    public void HeaderProtocolRejectsMissingMarkerAndInvalidFlowFields()
    {
        var codec = new ZlinkStreamHeaderCodec();
        Assert.Throws<ZlinkStreamException>(() => codec.Decode(new byte[] { 1, 1, 0, 1, (byte)'x' }));

        var invalid = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            "flow.test",
            ZlinkStreamMetadata.Empty,
            FlowId: "00000000-0000-4000-8000-000000000000",
            FlowOrigin: ZlinkStreamFlowOrigin.Application);
        Assert.Throws<ZlinkStreamException>(() => codec.Encode(invalid));
    }

    [Fact]
    public void HeaderProtocolRejectsUnknownFlag()
    {
        var codec = new ZlinkStreamHeaderCodec();
        var bytes = new byte[] { 1, 1, 0x80, 1, (byte)'x' };

        var exception = Assert.Throws<ZlinkStreamException>(() => codec.Decode(bytes));

        Assert.Equal(ZlinkStreamErrorCode.FrameDecodeFailed, exception.Error.Code);
    }

    [Fact]
    public void HeaderProtocolRejectsInvalidRequestSeqAndErrorCodecCombinations()
    {
        var codec = new ZlinkStreamHeaderCodec();

        var sendWithRequestSeq = new byte[12];
        sendWithRequestSeq[0] = (byte)ZlinkStreamMessageKind.Send;
        sendWithRequestSeq[1] = (byte)ZlinkStreamCodec.Json;
        sendWithRequestSeq[2] = (byte)ZlinkStreamHeaderFlags.HasRequestSeq;
        BinaryPrimitives.WriteUInt64BigEndian(sendWithRequestSeq.AsSpan(3, 8), 1);
        sendWithRequestSeq[11] = 1;

        Assert.Throws<ZlinkStreamException>(() => codec.Decode(sendWithRequestSeq));

        var responseWithoutRequestSeq = new byte[]
        {
            (byte)ZlinkStreamMessageKind.Response,
            (byte)ZlinkStreamCodec.Json,
            0,
            1,
            (byte)'x'
        };

        Assert.Throws<ZlinkStreamException>(() => codec.Decode(responseWithoutRequestSeq));

        var errorWithRawCodec = new byte[]
        {
            (byte)ZlinkStreamMessageKind.Error,
            (byte)ZlinkStreamCodec.Raw,
            0,
            1,
            (byte)'x'
        };

        Assert.Throws<ZlinkStreamException>(() => codec.Decode(errorWithRawCodec));
    }

    [Fact]
    public void HeaderProtocolEnforcesControlPacketContract()
    {
        var codec = new ZlinkStreamHeaderCodec();

        var control = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Control,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            "$zlink.heartbeat.ping",
            ZlinkStreamMetadata.Empty);
        var decoded = codec.Decode(codec.Encode(control));
        Assert.Equal(ZlinkStreamMessageKind.Control, decoded.Kind);
        Assert.Equal("$zlink.heartbeat.ping", decoded.Name);

        Assert.Throws<ZlinkStreamException>(() => codec.Encode(new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Control,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.None,
            null,
            "$zlink.heartbeat.ping",
            ZlinkStreamMetadata.Empty)));

        Assert.Throws<ZlinkStreamException>(() => codec.Encode(new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Control,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.PayloadCompressed,
            null,
            "$zlink.heartbeat.ping",
            ZlinkStreamMetadata.Empty)));

        Assert.Throws<ZlinkStreamException>(() => codec.Encode(new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Control,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            new ZlinkStreamRequestSeq(1),
            "$zlink.heartbeat.ping",
            ZlinkStreamMetadata.Empty)));

        Assert.Throws<ZlinkStreamException>(() => codec.Encode(new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Control,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            "$zlink.heartbeat.ping",
            ZlinkStreamMetadata.Empty.With("traceId", "abc"))));

        Assert.Throws<ZlinkStreamException>(() => codec.Encode(new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            "$zlink.user",
            ZlinkStreamMetadata.Empty)));
    }
}
