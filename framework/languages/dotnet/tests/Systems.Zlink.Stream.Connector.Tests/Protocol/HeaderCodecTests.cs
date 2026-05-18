using System.Buffers.Binary;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Contracts.Calls;
using Systems.Zlink.Stream.Connector.Runtime;
using Systems.Zlink.Stream.Connector.Runtime.Protocol.Framing;
using Xunit;


public sealed partial class StreamConnectorTests
{
    [Fact]
    public void HeaderCodecRoundTripsMetadataAndRequestSeq()
    {
        var codec = ZlinkStreamDefaultCodecFactory.Header();
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
    public void HeaderCodecRejectsUnknownFlag()
    {
        var codec = ZlinkStreamDefaultCodecFactory.Header();
        var bytes = new byte[] { 1, 1, 0x80, 1, (byte)'x' };

        var exception = Assert.Throws<ZlinkStreamException>(() => codec.Decode(bytes));

        Assert.Equal(ZlinkStreamErrorCode.FrameDecodeFailed, exception.Error.Code);
    }

    [Fact]
    public void HeaderCodecRejectsInvalidRequestSeqAndErrorCodecCombinations()
    {
        var codec = ZlinkStreamDefaultCodecFactory.Header();

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
    public void HeaderCodecEnforcesControlPacketContract()
    {
        var codec = ZlinkStreamDefaultCodecFactory.Header();

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
