using System.Text;
using System.Text.Json;

namespace Bingo.Server.Infrastructure;

public sealed class FileRegistryDiscoveryMetadata : IRegistryDiscoveryMetadata
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly string _directory;

    public FileRegistryDiscoveryMetadata(string directory)
    {
        _directory = directory;
        Directory.CreateDirectory(_directory);
    }

    public async ValueTask PutAsync(
        string key,
        IReadOnlyDictionary<string, string> metadata,
        CancellationToken cancellationToken)
    {
        var path = PathFor(key);
        var tempPath = $"{path}.{Guid.NewGuid():N}.tmp";
        var copy = new Dictionary<string, string>(metadata, StringComparer.Ordinal);
        await using (var stream = File.Create(tempPath))
        {
            await JsonSerializer.SerializeAsync(stream, copy, JsonOptions, cancellationToken)
                .ConfigureAwait(false);
        }

        File.Move(tempPath, path, overwrite: true);
    }

    public async ValueTask<IRegistryMetadataEntry> GetRequiredAsync(
        string key,
        CancellationToken cancellationToken)
    {
        var path = PathFor(key);
        if (!File.Exists(path))
        {
            throw new InvalidOperationException($"Registry metadata '{key}' was not found.");
        }

        var values = await ReadValuesAsync(path, key, cancellationToken)
            .ConfigureAwait(false);
        return new RegistryMetadataEntry(values);
    }

    public async ValueTask DeleteIfAsync(
        string key,
        IReadOnlyDictionary<string, string> expected,
        CancellationToken cancellationToken)
    {
        var path = PathFor(key);
        if (!File.Exists(path))
        {
            return;
        }

        var current = await ReadValuesAsync(path, key, cancellationToken).ConfigureAwait(false);
        if (ContainsAll(current, expected))
        {
            File.Delete(path);
        }
    }

    private static bool ContainsAll(
        IReadOnlyDictionary<string, string> current,
        IReadOnlyDictionary<string, string> expected)
    {
        foreach (var (key, value) in expected)
        {
            if (!current.TryGetValue(key, out var currentValue)
                || !string.Equals(currentValue, value, StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }

    private static async ValueTask<Dictionary<string, string>> ReadValuesAsync(
        string path,
        string key,
        CancellationToken cancellationToken)
    {
        await using var stream = File.OpenRead(path);
        var values = await JsonSerializer.DeserializeAsync<Dictionary<string, string>>(
            stream,
            JsonOptions,
            cancellationToken).ConfigureAwait(false);

        return values ?? throw new InvalidOperationException(
            $"Registry metadata '{key}' is empty.");
    }

    private string PathFor(string key)
    {
        var bytes = Encoding.UTF8.GetBytes(key);
        var name = Convert.ToBase64String(bytes)
            .Replace('+', '-')
            .Replace('/', '_')
            .TrimEnd('=');
        return Path.Combine(_directory, $"{name}.json");
    }
}
