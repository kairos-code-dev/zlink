using MessagePack;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Codecs;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Streams;
using StringValue = Google.Protobuf.WellKnownTypes.StringValue;

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
    public void CodecExtension_Can_Register_Custom_Serializer()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.Use(new MarkerCodecExtension());

        var custom = codecs.SingleCustomSerializer();

        Assert.NotNull(custom);
        Assert.Equal("application/avro", custom.Value.ContentType);
        Assert.IsType<MarkerSerializer>(custom.Value.Serializer);
    }

    [Fact]
    public void Protobuf_Extension_RoundTrips_Protobuf_Payload()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.Use(ZLinkProtobufCodec.Default);
        var value = new StringValue { Value = "hello" };

        var parts = ZLinkEnvelopeCodec.EncodeParts(CreateHeader(nameof(StringValue)), value, typeof(StringValue), codecs);

        Assert.Equal("application/x-protobuf", ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType);
        var decoded = Assert.IsType<StringValue>(ZLinkEnvelopeCodec.DecodeBody(parts, typeof(StringValue), codecs));
        Assert.Equal("hello", decoded.Value);
    }

    [Fact]
    public void MessagePack_Extension_RoundTrips_MessagePack_Payload()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.Use(ZLinkMessagePackCodec.Default);
        var value = new PackedProbe("hello");

        var parts = ZLinkEnvelopeCodec.EncodeParts(CreateHeader(nameof(PackedProbe)), value, typeof(PackedProbe), codecs);

        Assert.Equal("application/x-msgpack", ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType);
        var decoded = Assert.IsType<PackedProbe>(ZLinkEnvelopeCodec.DecodeBody(parts, typeof(PackedProbe), codecs));
        Assert.Equal(value, decoded);
    }

    [Fact]
    public void Binary_Extensions_Fall_Back_To_Json_For_Unsupported_Payload()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.Use(ZLinkProtobufCodec.Default);
        codecs.Use(ZLinkMessagePackCodec.Default);
        var value = new Probe("hello");

        var parts = ZLinkEnvelopeCodec.EncodeParts(CreateHeader(nameof(Probe)), value, typeof(Probe), codecs);

        Assert.Equal("application/json", ZLinkEnvelopeCodec.DecodeHeader(parts).ContentType);
        var decoded = Assert.IsType<Probe>(ZLinkEnvelopeCodec.DecodeBody(parts, typeof(Probe), codecs));
        Assert.Equal(value, decoded);
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
    public void StreamPayload_CustomSerializer_RoundTrips_Through_Framework_Message()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        codecs.AddSerializer("application/avro", new MarkerSerializer());
        codecs.AddStreamCodec("application/avro", ZlinkStreamCodec.Protobuf);

        var encoded = ZLinkStreamPacketPayloadCodec.Encode(new Probe("hello"), typeof(Probe), codecs);

        Assert.Equal(ZlinkStreamCodec.Protobuf, encoded.Codec);
        Assert.Equal("AVRO:hello", System.Text.Encoding.UTF8.GetString(encoded.Payload.Span));

        using var payload = Message.From(encoded.Payload.Span);
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            encoded.Codec,
            ZlinkStreamHeaderFlags.None,
            null,
            "orders.created",
            ZlinkStreamMetadata.Empty);

        var message = ZLinkStreamPacketPayloadCodec.DecodeMessage(header, payload, codecs);

        Assert.Equal("application/avro", message.ContentType);
        Assert.Equal(ZlinkStreamCodec.Protobuf, message.StreamCodec);
        Assert.Equal(new Probe("hello"), message.Decode<Probe>());
    }

    [Fact]
    public void StreamPayload_Missing_Codec_Extension_Fails_Decode()
    {
        var codecs = new ZLinkCodecRegistryBuilder();
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Protobuf,
            ZlinkStreamHeaderFlags.None,
            null,
            "orders.created",
            ZlinkStreamMetadata.Empty);

        using var payload = Message.From("AVRO:hello");
        var message = ZLinkStreamPacketPayloadCodec.DecodeMessage(header, payload, codecs);

        var error = Assert.Throws<InvalidOperationException>(() => message.Decode<Probe>());
        Assert.Contains("no matching codec extension is registered", error.Message, StringComparison.Ordinal);
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

    [MessagePackObject]
    public sealed record PackedProbe([property: Key(0)] string Text);

    private static ZLinkEnvelopeHeader CreateHeader(string messageName)
    {
        return new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            "orders",
            messageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null);
    }

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

    private sealed class MarkerCodecExtension : IZLinkCodecExtension
    {
        public void Register(IZLinkCodecRegistryBuilder codecs)
        {
            codecs.AddSerializer("application/avro", new MarkerSerializer());
        }
    }
}
