using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Codecs;

public sealed class CodecContracts
{
    [Fact]
    [ContractExample(typeof(IZLinkCodecRegistryBuilder))]
    public void Codec_registry_builder_declares_the_codecs_an_application_enables()
    {
        var codecs = new ExampleCodecRegistryBuilder();

        codecs.AddJson();
        codecs.AddMessagePack();
        codecs.AddProtobuf();

        Assert.Equal(["json", "message-pack", "protobuf"], codecs.EnabledCodecs);
    }

    [Fact]
    [ContractExample(typeof(IZLinkMessageSerializer))]
    public void Message_serializer_converts_business_objects_to_and_from_messages()
    {
        IZLinkMessageSerializer serializer = new ExampleMessageSerializer();

        using var encoded = serializer.Serialize(new Order("A-1", 3), typeof(Order));
        var decoded = (Order?)serializer.Deserialize(encoded, typeof(Order));

        Assert.Equal(new Order("A-1", 3), decoded);
    }

    private sealed record Order(string Sku, int Quantity);

    private sealed class ExampleMessageSerializer : IZLinkMessageSerializer
    {
        public Message Serialize(object value, Type type)
        {
            var order = (Order)value;
            return Message.From($"{order.Sku}:{order.Quantity}");
        }

        public object? Deserialize(Message message, Type type)
        {
            var parts = message.GetString().Split(':');
            return new Order(parts[0], int.Parse(parts[1]));
        }
    }

    private sealed class ExampleCodecRegistryBuilder : IZLinkCodecRegistryBuilder
    {
        private readonly List<string> _enabledCodecs = [];

        public IReadOnlyList<string> EnabledCodecs => _enabledCodecs;

        public void AddProtobuf() => _enabledCodecs.Add("protobuf");

        public void AddJson() => _enabledCodecs.Add("json");

        public void AddMessagePack() => _enabledCodecs.Add("message-pack");

        public void AddSerializer(string contentType, IZLinkMessageSerializer serializer) =>
            _enabledCodecs.Add(contentType);
    }
}
