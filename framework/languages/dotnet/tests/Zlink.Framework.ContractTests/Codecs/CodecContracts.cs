using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Codecs;

public sealed class CodecContracts
{
    [Fact]
    [ContractExample(typeof(IZLinkCodecRegistryBuilder), typeof(IZLinkCodecExtension))]
    public void Codec_registry_builder_registers_extensions_and_serializers()
    {
        var codecs = new ExampleCodecRegistryBuilder();

        codecs.AddJson();
        codecs.Use(new ExampleCodecExtension());

        Assert.Equal(["json", "application/example"], codecs.EnabledCodecs);
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

        public void Use(IZLinkCodecExtension extension) => extension.Register(this);

        public void AddJson() => _enabledCodecs.Add("json");

        public void AddSerializer(string contentType, IZLinkMessageSerializer serializer) =>
            _enabledCodecs.Add(contentType);

        public void AddSerializer(
            string contentType,
            IZLinkMessageSerializer serializer,
            Func<Type, bool> canSerialize) =>
            _enabledCodecs.Add(contentType);

        public void AddStreamCodec(
            string contentType,
            Systems.Zlink.Stream.Connector.Contracts.ZlinkStreamCodec codec) { }
    }

    private sealed class ExampleCodecExtension : IZLinkCodecExtension
    {
        public void Register(IZLinkCodecRegistryBuilder codecs)
        {
            codecs.AddSerializer("application/example", new ExampleMessageSerializer());
        }
    }
}
