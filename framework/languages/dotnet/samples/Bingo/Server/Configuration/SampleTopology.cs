using Systems.Zlink;
using Microsoft.Extensions.Configuration;

namespace Bingo.Server.Configuration;

public sealed record SampleTopology(
    SampleApiNode ApiA,
    SampleApiNode ApiB,
    SamplePlayNode PlayA,
    SamplePlayNode PlayB,
    SampleSessionNode SessionA,
    SampleSessionNode SessionB,
    string RedisEndpoint,
    string RedisKeyPrefix)
{
    public static SampleRuntimeConfiguration Load(string[] args)
    {
        if (args.Length != 2 || args[0] != "--config")
            throw new ArgumentException("Usage: --config PATH");

        var settings = new ConfigurationBuilder()
                           .AddJsonFile(Path.GetFullPath(args[1]), optional: false, reloadOnChange: false)
                           .Build()
                           .GetRequiredSection("Sample")
                           .Get<SampleConfiguration>()
                       ?? throw new InvalidOperationException("Bingo Sample configuration is empty.");
        settings.Validate();

        var playA = new SamplePlayNode(
            settings.PlayAChannelEndpoint,
            settings.PlayBChannelEndpoint,
            settings.PlayASpotEndpoint,
            settings.PlayBSpotEndpoint,
            settings.PlayASpotRouterEndpoint,
            RoutingId.From("2201"));
        var playB = new SamplePlayNode(
            settings.PlayBChannelEndpoint,
            settings.PlayAChannelEndpoint,
            settings.PlayBSpotEndpoint,
            settings.PlayASpotEndpoint,
            settings.PlayBSpotRouterEndpoint,
            RoutingId.From("2202"));

        var topology = new SampleTopology(
            new SampleApiNode(
                settings.ApiAChannelEndpoint,
                RoutingId.From("3301")),
            new SampleApiNode(
                settings.ApiBChannelEndpoint,
                RoutingId.From("3302")),
            playA,
            playB,
            new SampleSessionNode(
                settings.SessionASpotEndpoint,
                settings.SessionARouterEndpoint,
                settings.SessionAStreamEndpoint,
                RoutingId.From("1101"),
                RoutingId.From("1102"),
                playA.NodeRid,
                playA.SpotRouterEndpoint,
                playA.PlayChannelEndpoint),
            new SampleSessionNode(
                settings.SessionBSpotEndpoint,
                settings.SessionBRouterEndpoint,
                settings.SessionBStreamEndpoint,
                RoutingId.From("1103"),
                RoutingId.From("1104"),
                playB.NodeRid,
                playB.SpotRouterEndpoint,
                playB.PlayChannelEndpoint),
            settings.RedisEndpoint,
            settings.RedisKeyPrefix);
        return new SampleRuntimeConfiguration(topology, settings.NodeName, settings.LogDirectory);
    }

    public SampleApiNode Api(string nodeName)
    {
        return string.Equals(nodeName, "b", StringComparison.OrdinalIgnoreCase) ? ApiB : ApiA;
    }

    public SamplePlayNode Play(string nodeName)
    {
        return string.Equals(nodeName, "b", StringComparison.OrdinalIgnoreCase) ? PlayB : PlayA;
    }

    public SampleSessionNode Session(string nodeName)
    {
        return string.Equals(nodeName, "b", StringComparison.OrdinalIgnoreCase) ? SessionB : SessionA;
    }

}

public sealed record SampleRuntimeConfiguration(
    SampleTopology Topology,
    string NodeName,
    string LogDirectory);

public sealed class SampleConfiguration
{
    public string NodeName { get; init; } = "";
    public string LogDirectory { get; init; } = "";
    public string RedisEndpoint { get; init; } = "";
    public string RedisKeyPrefix { get; init; } = "";
    public string ApiAChannelEndpoint { get; init; } = "";
    public string ApiBChannelEndpoint { get; init; } = "";
    public string PlayAChannelEndpoint { get; init; } = "";
    public string PlayBChannelEndpoint { get; init; } = "";
    public string PlayASpotEndpoint { get; init; } = "";
    public string PlayBSpotEndpoint { get; init; } = "";
    public string PlayASpotRouterEndpoint { get; init; } = "";
    public string PlayBSpotRouterEndpoint { get; init; } = "";
    public string SessionASpotEndpoint { get; init; } = "";
    public string SessionBSpotEndpoint { get; init; } = "";
    public string SessionARouterEndpoint { get; init; } = "";
    public string SessionBRouterEndpoint { get; init; } = "";
    public string SessionAStreamEndpoint { get; init; } = "";
    public string SessionBStreamEndpoint { get; init; } = "";

    public void Validate()
    {
        foreach (var (name, value) in new Dictionary<string, string>
                 {
                     [nameof(NodeName)] = NodeName,
                     [nameof(LogDirectory)] = LogDirectory,
                     [nameof(RedisEndpoint)] = RedisEndpoint,
                     [nameof(RedisKeyPrefix)] = RedisKeyPrefix,
                     [nameof(ApiAChannelEndpoint)] = ApiAChannelEndpoint,
                     [nameof(ApiBChannelEndpoint)] = ApiBChannelEndpoint,
                     [nameof(PlayAChannelEndpoint)] = PlayAChannelEndpoint,
                     [nameof(PlayBChannelEndpoint)] = PlayBChannelEndpoint,
                     [nameof(PlayASpotEndpoint)] = PlayASpotEndpoint,
                     [nameof(PlayBSpotEndpoint)] = PlayBSpotEndpoint,
                     [nameof(PlayASpotRouterEndpoint)] = PlayASpotRouterEndpoint,
                     [nameof(PlayBSpotRouterEndpoint)] = PlayBSpotRouterEndpoint,
                     [nameof(SessionASpotEndpoint)] = SessionASpotEndpoint,
                     [nameof(SessionBSpotEndpoint)] = SessionBSpotEndpoint,
                     [nameof(SessionARouterEndpoint)] = SessionARouterEndpoint,
                     [nameof(SessionBRouterEndpoint)] = SessionBRouterEndpoint,
                     [nameof(SessionAStreamEndpoint)] = SessionAStreamEndpoint,
                     [nameof(SessionBStreamEndpoint)] = SessionBStreamEndpoint
                 })
            if (string.IsNullOrWhiteSpace(value))
                throw new InvalidOperationException($"Bingo Sample.{name} is required.");
    }
}

public sealed record SampleApiNode(
    string ChannelEndpoint,
    RoutingId RouteRid);

public sealed record SamplePlayNode(
    string PlayChannelEndpoint,
    string PeerPlayChannelEndpoint,
    string SpotPubEndpoint,
    string PeerSpotPubEndpoint,
    string SpotRouterEndpoint,
    RoutingId NodeRid);

public sealed record SampleSessionNode(
    string PubEndpoint,
    string RouterEndpoint,
    string StreamEndpoint,
    RoutingId RoutingId,
    RoutingId PublisherRoutingId,
    RoutingId PreferredPlayNodeRid,
    string PreferredPlaySpotRouterEndpoint,
    string PreferredPlayChannelEndpoint);
