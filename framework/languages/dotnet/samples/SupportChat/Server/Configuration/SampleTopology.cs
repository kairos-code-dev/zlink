using Microsoft.Extensions.Configuration;
using Systems.Zlink;

namespace SupportChat.Server.Configuration;

public sealed record SampleTopology(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string ApiChannelEndpoint,
    string SupportChannelEndpoint,
    string SessionSpotEndpoint,
    string SessionRouterEndpoint,
    string SupportEntrySpotEndpoint,
    string SupportEntrySpotRouterEndpoint,
    string StreamEndpoint,
    RoutingId SessionRouterRid,
    RoutingId SessionPubRid,
    RoutingId SupportEntryRid)
{
    public SampleSessionNode PrimarySession => new(
        SessionSpotEndpoint,
        SessionRouterEndpoint,
        StreamEndpoint,
        SessionRouterRid,
        SessionPubRid);

    public static SampleRuntimeConfiguration Load(string[] args)
    {
        if (args.Length != 2 || args[0] != "--config")
            throw new ArgumentException("Usage: --config PATH");
        var settings = new ConfigurationBuilder()
                           .AddJsonFile(Path.GetFullPath(args[1]), optional: false, reloadOnChange: false)
                           .Build()
                           .GetRequiredSection("Sample")
                           .Get<SampleConfiguration>()
                       ?? throw new InvalidOperationException("SupportChat Sample configuration is empty.");
        settings.Validate();
        var topology = new SampleTopology(
            settings.RedisEndpoint,
            settings.RedisKeyPrefix,
            settings.ApiChannelEndpoint,
            settings.SupportChannelEndpoint,
            settings.SessionSpotEndpoint,
            settings.SessionRouterEndpoint,
            settings.SupportEntrySpotEndpoint,
            settings.SupportEntrySpotRouterEndpoint,
            settings.StreamEndpoint,
            RoutingId.From("3101"),
            RoutingId.From("3102"),
            RoutingId.From("4201"));
        return new SampleRuntimeConfiguration(topology, settings.LogDirectory);
    }
}

public sealed record SampleRuntimeConfiguration(SampleTopology Topology, string LogDirectory);

public sealed class SampleConfiguration
{
    public string LogDirectory { get; init; } = "";
    public string RedisEndpoint { get; init; } = "";
    public string RedisKeyPrefix { get; init; } = "";
    public string ApiChannelEndpoint { get; init; } = "";
    public string SupportChannelEndpoint { get; init; } = "";
    public string SessionSpotEndpoint { get; init; } = "";
    public string SessionRouterEndpoint { get; init; } = "";
    public string SupportEntrySpotEndpoint { get; init; } = "";
    public string SupportEntrySpotRouterEndpoint { get; init; } = "";
    public string StreamEndpoint { get; init; } = "";

    public void Validate()
    {
        foreach (var value in GetType().GetProperties().Select(property =>
                     (property.Name, Value: (string?)property.GetValue(this))))
            if (string.IsNullOrWhiteSpace(value.Value))
                throw new InvalidOperationException($"SupportChat Sample.{value.Name} is required.");
    }
}

public sealed record SampleSessionNode(
    string PubEndpoint,
    string RouterEndpoint,
    string StreamEndpoint,
    RoutingId RoutingId,
    RoutingId PublisherRoutingId);
