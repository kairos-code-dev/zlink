using System.Buffers.Binary;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Builders;
using Systems.Zlink.Stream.Connector.Codecs;
using Systems.Zlink.Stream.Connector.Compression;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Framing;
using Systems.Zlink.Stream.Connector.Headers;
using Systems.Zlink.Stream.Connector.Metadata;
using Systems.Zlink.Stream.Connector.Options;
using Xunit;


public sealed partial class StreamConnectorTests
{
    [Fact]
    public void HeaderCodecRoundTripsMetadataAndRequestId()
    {
        var codec = new ZlinkStreamHeaderCodec();
        var source = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRid,
            new ZlinkStreamRequestId(42),
            "profile.get",
            ZlinkStreamMetadata.Empty.With("traceId", "abc"));

        var decoded = codec.Decode(codec.Encode(source));

        Assert.Equal(source.Kind, decoded.Kind);
        Assert.Equal(source.Codec, decoded.Codec);
        Assert.Equal(source.RequestId, decoded.RequestId);
        Assert.Equal(source.Name, decoded.Name);
        Assert.Equal("abc", decoded.Metadata.Get("traceId"));
    }

    [Fact]
    public void HeaderCodecRejectsUnknownFlag()
    {
        var codec = new ZlinkStreamHeaderCodec();
        var bytes = new byte[] { 1, 1, 0x80, 1, (byte)'x' };

        var exception = Assert.Throws<ZlinkStreamException>(() => codec.Decode(bytes));

        Assert.Equal(ZlinkStreamErrorCode.FrameDecodeFailed, exception.Error.Code);
    }

    [Fact]
    public void HeaderCodecRejectsInvalidRidAndErrorCodecCombinations()
    {
        var codec = new ZlinkStreamHeaderCodec();

        var sendWithRid = new byte[12];
        sendWithRid[0] = (byte)ZlinkStreamMessageKind.Send;
        sendWithRid[1] = (byte)ZlinkStreamCodec.Json;
        sendWithRid[2] = (byte)ZlinkStreamHeaderFlags.HasRid;
        BinaryPrimitives.WriteUInt64BigEndian(sendWithRid.AsSpan(3, 8), 1);
        sendWithRid[11] = 1;

        Assert.Throws<ZlinkStreamException>(() => codec.Decode(sendWithRid));

        var responseWithoutRid = new byte[]
        {
            (byte)ZlinkStreamMessageKind.Response,
            (byte)ZlinkStreamCodec.Json,
            0,
            1,
            (byte)'x'
        };

        Assert.Throws<ZlinkStreamException>(() => codec.Decode(responseWithoutRid));

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

}
