using PubSub.Server.Subscriber.Configuration;
using PubSub.Server.Subscriber.Handlers;
using PubSub.Server.Subscriber;

namespace PubSub.Server.Subscriber.Configuration;

internal sealed record SubscriberOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string RegistryRouterEndpoint,
    string PublisherEndpoint,
    string? EvidenceFile,
    int HandlerDelayMs)
{
    public static SubscriberOptions Parse(string[] args)
    {
        var values = ServerArgs.Parse(args);
        return new SubscriberOptions(
            Rid: values.Get("--rid") ?? "subscriber",
            HttpUrl: values.Get("--http-url") ?? "http://127.0.0.1:0",
            LogDir: values.Get("--log-dir") ?? "logs",
            RegistryRouterEndpoint: values.Require("--registry-router-endpoint"),
            PublisherEndpoint: values.Require("--publisher-endpoint"),
            EvidenceFile: values.Get("--evidence-file"),
            HandlerDelayMs: int.TryParse(values.Get("--handler-delay-ms"), out var delayMs) ? delayMs : 0);
    }
}
