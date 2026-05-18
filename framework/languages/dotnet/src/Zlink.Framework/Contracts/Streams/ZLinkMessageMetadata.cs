namespace Zlink.Framework.Contracts.Streams;

public sealed class ZLinkMessageMetadata(
    IReadOnlyDictionary<string, string> application,
    IReadOnlyDictionary<string, string> codec)
{
    public static ZLinkMessageMetadata Empty { get; } = new(
        new Dictionary<string, string>(StringComparer.Ordinal),
        new Dictionary<string, string>(StringComparer.Ordinal));

    public IReadOnlyDictionary<string, string> Application { get; } = application;

    public IReadOnlyDictionary<string, string> Codec { get; } = codec;

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
