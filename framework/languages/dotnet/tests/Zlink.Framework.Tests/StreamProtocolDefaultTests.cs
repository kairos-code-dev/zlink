using Systems.Zlink.Stream.Connector.Contracts;

namespace Zlink.Framework.Tests;

public sealed class StreamProtocolDefaultTests
{
    [Fact]
    public void FrameworkDefaultHeaderCodec_MatchesConnectorDefaultHeaderCodec()
    {
        var connectorCodec = new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri("tcp://127.0.0.1:1")
        }.HeaderCodec;
        var frameworkCodec = ZLinkStreamProtocolDefaults.HeaderCodec;
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Request,
            ZlinkStreamCodec.Json,
            ZlinkStreamHeaderFlags.HasRequestSeq | ZlinkStreamHeaderFlags.HasMetadata,
            new ZlinkStreamRequestSeq(42),
            "profile.get",
            ZlinkStreamMetadata.Empty.With("traceId", "abc"));

        var encodedByConnector = connectorCodec.Encode(header);
        var encodedByFramework = frameworkCodec.Encode(header);
        var decodedByConnector = connectorCodec.Decode(encodedByFramework);
        var decodedByFramework = frameworkCodec.Decode(encodedByConnector);

        Assert.Equal(encodedByConnector.ToArray(), encodedByFramework.ToArray());
        AssertDecodedHeader(header, decodedByConnector);
        AssertDecodedHeader(header, decodedByFramework);
    }

    private static void AssertDecodedHeader(ZlinkStreamHeader expected, ZlinkStreamHeader actual)
    {
        Assert.Equal(expected.Kind, actual.Kind);
        Assert.Equal(expected.Codec, actual.Codec);
        Assert.Equal(expected.Flags, actual.Flags);
        Assert.Equal(expected.RequestSeq, actual.RequestSeq);
        Assert.Equal(expected.Name, actual.Name);
        Assert.Equal(expected.Metadata.Values, actual.Metadata.Values);
    }
}
