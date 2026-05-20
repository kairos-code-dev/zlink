namespace Bingo.Server.Infrastructure;

public sealed class RegistryMetadataEntry(IReadOnlyDictionary<string, string> metadata) : IRegistryMetadataEntry
{
    private readonly IReadOnlyDictionary<string, string> _metadata = new Dictionary<string, string>(metadata, StringComparer.Ordinal);

    public string Require(string name)
    {
        if (!_metadata.TryGetValue(name, out var value))
        {
            throw new InvalidOperationException($"Registry metadata value '{name}' was not found.");
        }

        return value;
    }

    public bool ContainsAll(IReadOnlyDictionary<string, string> expected)
    {
        foreach (var (name, value) in expected)
        {
            if (!_metadata.TryGetValue(name, out var current)
                || !string.Equals(current, value, StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }
}
