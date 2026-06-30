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
            values.Get("--rid") ?? "subscriber",
            values.Get("--http-url") ?? "http://127.0.0.1:0",
            values.Get("--log-dir") ?? "logs",
            values.Require("--registry-router-endpoint"),
            values.Require("--publisher-endpoint"),
            values.Get("--evidence-file"),
            int.TryParse(values.Get("--handler-delay-ms"), out var delayMs) ? delayMs : 0);
    }
}