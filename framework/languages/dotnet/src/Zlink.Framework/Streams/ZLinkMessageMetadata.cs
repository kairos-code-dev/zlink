namespace Zlink.Framework.Streams;

public sealed class ZLinkMessageMetadata
{
    public static ZLinkMessageMetadata Empty { get; } = new(
        new Dictionary<string, string>(StringComparer.Ordinal),
        new Dictionary<string, string>(StringComparer.Ordinal));

    public ZLinkMessageMetadata(
        IReadOnlyDictionary<string, string> application,
        IReadOnlyDictionary<string, string> codec)
    {
        Application = application;
        Codec = codec;
    }

    public IReadOnlyDictionary<string, string> Application { get; }

    public IReadOnlyDictionary<string, string> Codec { get; }

    public bool TryGetApplicationValue(
        string key,
        out string? value)
    {
        return Application.TryGetValue(key, out value);
    }

    public bool TryGetCodecValue(
        string key,
        out string? value)
    {
        return Codec.TryGetValue(key, out value);
    }
}

public interface IZLinkMessageMetadataPolicy
{
    bool CanForwardApplicationKey(string key);
}
