namespace Zlink.Framework;

internal sealed class ZLinkCodecRegistryBuilder : IZLinkCodecRegistryBuilder
{
    private readonly HashSet<string> _codecs = new(StringComparer.Ordinal);

    public IReadOnlyCollection<string> RegisteredCodecs => _codecs;

    public void AddProtobuf()
    {
        _codecs.Add("protobuf");
    }

    public void AddJson()
    {
        _codecs.Add("json");
    }

    public void AddMessagePack()
    {
        _codecs.Add("messagepack");
    }
}
