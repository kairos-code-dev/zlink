namespace RegistryMessaging.Client;

internal sealed record ClientOptions(string DriverUrl)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            values[key[2..]] = args[++i];
        }

        return new ClientOptions(
            values.TryGetValue("driver-url", out var driverUrl) && !string.IsNullOrWhiteSpace(driverUrl)
                ? driverUrl
                : throw new ArgumentException("--driver-url is required."));
    }
}
