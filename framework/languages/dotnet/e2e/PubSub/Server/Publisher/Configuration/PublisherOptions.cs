using PubSub.Server.Publisher.Configuration;
using PubSub.Server.Publisher.Endpoints;
using PubSub.Server.Publisher;

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
            Rid: values.Get("--rid") ?? "publisher",
            HttpUrl: values.Get("--http-url") ?? "http://127.0.0.1:0",
            LogDir: values.Get("--log-dir") ?? "logs",
            RegistryRouterEndpoint: values.Require("--registry-router-endpoint"),
            PublisherEndpoint: values.Require("--publisher-endpoint"),
            EvidenceFile: values.Get("--evidence-file"));
    }
}
