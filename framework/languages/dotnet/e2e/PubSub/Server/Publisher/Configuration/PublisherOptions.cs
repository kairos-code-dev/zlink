namespace PubSub.Server.Publisher.Configuration;

internal sealed record PublisherOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string RegistryRouterEndpoint,
    string PublisherEndpoint,
    string? EvidenceFile)
{
    public static PublisherOptions Parse(string[] args)
    {
        var values = ServerArgs.Parse(args);
        return new PublisherOptions(
            values.Get("--rid") ?? "publisher",
            values.Get("--http-url") ?? "http://127.0.0.1:0",
            values.Get("--log-dir") ?? "logs",
            values.Require("--registry-router-endpoint"),
            values.Require("--publisher-endpoint"),
            values.Get("--evidence-file"));
    }
}