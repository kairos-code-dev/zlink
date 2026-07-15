using Microsoft.Extensions.Configuration;

namespace Bingo.Server.Configuration;

public static class SampleConfigurationLoader
{
    public static SampleRuntimeConfiguration<SampleApiNode> LoadApi(string[] args)
    {
        var settings = Load(args);
        settings.ValidateCommon();
        return settings.ToRuntime(new SampleApiNode(
            settings.Require(nameof(settings.ChannelEndpoint), settings.ChannelEndpoint),
            settings.Require(nameof(settings.SpotEndpoint), settings.SpotEndpoint),
            settings.Require(nameof(settings.SpotRouterEndpoint), settings.SpotRouterEndpoint)));
    }

    public static SampleRuntimeConfiguration<SamplePlayNode> LoadPlay(string[] args)
    {
        var settings = Load(args);
        settings.ValidateCommon();
        return settings.ToRuntime(new SamplePlayNode(
            settings.Require(nameof(settings.SpotEndpoint), settings.SpotEndpoint),
            settings.Require(nameof(settings.SpotRouterEndpoint), settings.SpotRouterEndpoint)));
    }

    public static SampleRuntimeConfiguration<SampleSessionNode> LoadSession(string[] args)
    {
        var settings = Load(args);
        settings.ValidateCommon();
        return settings.ToRuntime(new SampleSessionNode(
            settings.Require(nameof(settings.SpotEndpoint), settings.SpotEndpoint),
            settings.Require(nameof(settings.SpotRouterEndpoint), settings.SpotRouterEndpoint),
            settings.Require(nameof(settings.StreamEndpoint), settings.StreamEndpoint)));
    }

    private static SampleConfiguration Load(string[] args)
    {
        if (args.Length != 2 || args[0] != "--config")
            throw new ArgumentException("Usage: --config PATH");

        return new ConfigurationBuilder()
                   .AddJsonFile(Path.GetFullPath(args[1]), optional: false, reloadOnChange: false)
                   .Build()
                   .GetRequiredSection("Sample")
                   .Get<SampleConfiguration>()
               ?? throw new InvalidOperationException("Bingo Sample configuration is empty.");
    }
}

public sealed record SampleRuntimeConfiguration<TNode>(
    TNode Node,
    string NodeName,
    string LogDirectory,
    string RedisEndpoint,
    string RedisKeyPrefix);

public sealed class SampleConfiguration
{
    public string NodeName { get; init; } = "";
    public string LogDirectory { get; init; } = "";
    public string RedisEndpoint { get; init; } = "";
    public string RedisKeyPrefix { get; init; } = "";
    public string ChannelEndpoint { get; init; } = "";
    public string SpotEndpoint { get; init; } = "";
    public string SpotRouterEndpoint { get; init; } = "";
    public string StreamEndpoint { get; init; } = "";

    public void ValidateCommon()
    {
        Require(nameof(NodeName), NodeName);
        Require(nameof(LogDirectory), LogDirectory);
        Require(nameof(RedisEndpoint), RedisEndpoint);
        Require(nameof(RedisKeyPrefix), RedisKeyPrefix);
        if (NodeName is not ("a" or "b"))
            throw new InvalidOperationException("Bingo Sample.NodeName must be 'a' or 'b'.");
    }

    public string Require(string name, string value)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"Bingo Sample.{name} is required.")
            : value;
    }

    public SampleRuntimeConfiguration<TNode> ToRuntime<TNode>(TNode node)
    {
        return new SampleRuntimeConfiguration<TNode>(
            node,
            NodeName,
            LogDirectory,
            RedisEndpoint,
            RedisKeyPrefix);
    }
}

public sealed record SampleApiNode(
    string ChannelEndpoint,
    string SpotPubEndpoint,
    string SpotRouterEndpoint);

public sealed record SamplePlayNode(
    string SpotPubEndpoint,
    string SpotRouterEndpoint);

public sealed record SampleSessionNode(
    string PubEndpoint,
    string RouterEndpoint,
    string StreamEndpoint);
