using Zlink.Framework.Contracts.Codecs;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class CustomSerializerEnvelopeTests
{
    [Fact]
    public void CustomSerializer_Sets_ContentType_And_RoundTrips_Body()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/avro", new MarkerSerializer());

        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "orders",
            nameof(Probe),
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null);

        var parts = ZLinkEnvelopeCodec.EncodeParts(header, new Probe("hello"), typeof(Probe), codecs);

        // The custom serializer's content type is carried on the envelope header.
        var decodedHeader = ZLinkEnvelopeCodec.DecodeHeader(parts);
        Assert.Equal("application/avro", decodedHeader.ContentType);

        // The body is encoded with the custom serializer, not JSON.
        Assert.Equal("AVRO:hello", parts[1].GetString());

        // And it decodes back through the same registered serializer.
        var decoded = ZLinkEnvelopeCodec.DecodeBody(parts, typeof(Probe), codecs);
        Assert.Equal(new Probe("hello"), decoded);
    }

    [Fact]
    public void Without_Custom_Serializer_Body_Stays_Json()
    {
        var codecs = new ZLinkCodecRegistryBuilder();

        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "orders",
            nameof(Probe),
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null);

        var parts = ZLinkEnvelopeCodec.EncodeParts(header, new Probe("hello"), typeof(Probe), codecs);

        Assert.Equal("application/json", ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType);
        var decoded = ZLinkEnvelopeCodec.DecodeBody(parts, typeof(Probe), codecs);
        Assert.Equal(new Probe("hello"), decoded);
    }

    [Fact]
    public void Two_Custom_Serializers_Are_Ambiguous()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/avro", new MarkerSerializer());
        codecs.AddSerializer("application/thrift", new MarkerSerializer());

        Assert.Throws<InvalidOperationException>(() => codecs.SingleCustomSerializer());
    }

    private sealed record Probe(string Text);

    private sealed class MarkerSerializer : IZLinkMessageSerializer
    {
        public Message Serialize(object value, Type type)
        {
            var probe = (Probe)value;
            return Message.From("AVRO:" + probe.Text);
        }

        public object? Deserialize(Message message, Type type)
        {
            var text = message.GetString();
            var value = text.StartsWith("AVRO:", StringComparison.Ordinal) ? text["AVRO:".Length..] : text;
            return new Probe(value);
        }
    }
}
