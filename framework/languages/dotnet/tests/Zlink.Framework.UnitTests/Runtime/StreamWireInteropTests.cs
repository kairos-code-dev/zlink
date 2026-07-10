using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using ConnectorFrameCodec = Systems.Zlink.Stream.Connector.Runtime.Protocol.Framing.ZlinkStreamFrameCodec;
using ConnectorHeaderCodec = Systems.Zlink.Stream.Connector.Runtime.Protocol.ZlinkStreamHeaderCodec;
using CoreFrameCodec = Zlink.Framework.Runtime.Streams.ZLinkStreamFrameCodec;
using CoreHeaderCodec = Zlink.Framework.Runtime.Streams.ZLinkStreamHeaderCodec;

namespace Zlink.Framework.UnitTests;

public sealed class StreamWireInteropTests
{
    [Fact]
    public void CoreAndConnectorHeaderCodecs_EncodeSameBytes()
    {
        var header = CreateHeader();

        var core = CoreHeaderCodec.Encode(header);
        var connector = new ConnectorHeaderCodec().Encode(header);

        Assert.Equal(core.ToArray(), connector.ToArray());
    }

    [Fact]
    public void CoreHeaderCodec_RoundTripsEmptyMetadataValue()
    {
        var header = CreateHeader();

        var decoded = CoreHeaderCodec.Decode(CoreHeaderCodec.Encode(header));

        Assert.Equal(string.Empty, decoded.Metadata.Values["optional"]);
    }

    [Fact]
    public void ConnectorHeaderEncoding_DecodesWithCoreHeaderCodec()
    {
        var encoded = new ConnectorHeaderCodec().Encode(CreateHeader());

        var decoded = CoreHeaderCodec.Decode(encoded);

        Assert.Equal(string.Empty, decoded.Metadata.Values["optional"]);
    }

    [Fact]
    public void ConnectorFrameEncoding_DecodesWithCoreFrameCodec()
    {
        var header = new ConnectorHeaderCodec().Encode(CreateHeader());
        var payload = Encoding.UTF8.GetBytes("payload");
        var frame = new byte[ConnectorFrameCodec.GetFrameSize(header.Length, payload.Length)];

        ConnectorFrameCodec.WriteFrame(frame, header, payload);

        Assert.True(CoreFrameCodec.TryDecode(frame, out var decodedHeader, out var decodedPayload));
        Assert.Equal(header.ToArray(), decodedHeader.ToArray());
        Assert.Equal(payload, decodedPayload.ToArray());
    }

    [Fact]
    public void CoreAndConnectorFrameCodecs_EncodeSameBytes()
    {
        var header = new ConnectorHeaderCodec().Encode(CreateHeader());
        var payload = Encoding.UTF8.GetBytes("payload");
        var connector = new byte[ConnectorFrameCodec.GetFrameSize(header.Length, payload.Length)];

        ConnectorFrameCodec.WriteFrame(connector, header, payload);
        var core = CoreFrameCodec.Encode(header.Span, payload);

        Assert.Equal(core, connector);
    }

    private static ZlinkStreamHeader CreateHeader()
    {
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq | ZlinkStreamHeaderFlags.PayloadCompressed,
            new ZlinkStreamRequestSeq(42),
            "profile.get",
            ZlinkStreamMetadata.Empty
                .With("traceId", "abc")
                .With("optional", ""),
            "corr-1");
    }
}
