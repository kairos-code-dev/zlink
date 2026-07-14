using Systems.Zlink;
using System.Text.Json;

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
        var index = Array.IndexOf(args, "--config");
        if (index < 0 || index + 1 >= args.Length)
            throw new ArgumentException("Usage: --config PATH");
        var document = JsonSerializer.Deserialize<SampleConfigurationDocument>(
                           File.ReadAllText(args[index + 1]),
                           new JsonSerializerOptions { PropertyNameCaseInsensitive = true })
                       ?? throw new InvalidOperationException("SupportChat configuration is empty.");
        var settings = document.Sample;
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

public sealed class SampleConfigurationDocument
{
    public SampleConfiguration Sample { get; init; } = new();
}

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
