namespace PubSub.Server.Configuration;

public sealed class ServerArgs
{
    private readonly Dictionary<string, List<string>> _values;

    private ServerArgs(Dictionary<string, List<string>> values)
    {
        _values = values;
    }

    public static ServerArgs Parse(string[] args)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        return new ServerArgs(values);
    }

    public string? Get(string name) => _values.TryGetValue(name, out var bucket) ? bucket[^1] : null;

    public string Require(string name) => Get(name)
        ?? throw new ArgumentException($"{name} is required.");
}
