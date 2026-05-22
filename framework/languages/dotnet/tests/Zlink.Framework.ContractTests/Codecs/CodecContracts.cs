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

    private sealed class ExampleCodecRegistryBuilder : IZLinkCodecRegistryBuilder
    {
        private readonly List<string> _enabledCodecs = [];

        public IReadOnlyList<string> EnabledCodecs => _enabledCodecs;

        public void AddProtobuf() => _enabledCodecs.Add("protobuf");

        public void AddJson() => _enabledCodecs.Add("json");

        public void AddMessagePack() => _enabledCodecs.Add("message-pack");
    }
}
