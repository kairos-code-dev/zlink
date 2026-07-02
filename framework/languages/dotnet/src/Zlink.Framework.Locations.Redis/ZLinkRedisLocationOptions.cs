using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// Connection and key namespace settings for the official Redis location
/// store. Every location row, generation counter, owner lease, index, and
/// change stamp lives under <see cref="KeyPrefix"/>, so two deployments (or
/// two test runs) sharing one Redis stay isolated by using distinct prefixes.
/// </summary>
public sealed class ZLinkRedisLocationOptions
{
    /// <summary>StackExchange.Redis connection string, for example
    /// "127.0.0.1:6379". Ignored when <see cref="ConfigurationOptions"/> is
    /// set. One of the two must be provided.</summary>
    public string? ConnectionString { get; set; }

    /// <summary>Full connection configuration for callers that need more
    /// than a connection string. Takes precedence over
    /// <see cref="ConnectionString"/>.</summary>
    public ConfigurationOptions? ConfigurationOptions { get; set; }

    /// <summary>Required key namespace, for example "zlink:e2e". The store
    /// never reads or writes keys outside this prefix.</summary>
    public string KeyPrefix { get; set; } = string.Empty;

    internal ConfigurationOptions BuildConfiguration()
    {
        var configuration = ConfigurationOptions is { } explicitOptions
            ? explicitOptions.Clone()
            : ConfigurationOptions.Parse(ConnectionString!);

        // The store maps command failures to StoreUnavailable / read
        // exceptions itself; the multiplexer must keep reconnecting in the
        // background instead of failing construction permanently.
        configuration.AbortOnConnectFail = false;
        return configuration;
    }

    internal void Validate()
    {
        if (string.IsNullOrEmpty(KeyPrefix))
        {
            throw new ArgumentException(
                "ZLinkRedisLocationOptions.KeyPrefix is required. Pick a namespace such as \"zlink:e2e\".",
                nameof(KeyPrefix));
        }

        if (ConfigurationOptions is null && string.IsNullOrEmpty(ConnectionString))
        {
            throw new ArgumentException(
                "ZLinkRedisLocationOptions requires ConnectionString or ConfigurationOptions.",
                nameof(ConnectionString));
        }
    }
}
