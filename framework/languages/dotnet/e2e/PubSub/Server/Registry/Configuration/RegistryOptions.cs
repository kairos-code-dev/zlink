namespace PubSub.Server.Registry.Configuration;

internal sealed record RegistryOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string RegistryPubEndpoint,
    string RegistryRouterEndpoint)
{
    public static RegistryOptions Parse(string[] args)
    {
        var values = ServerArgs.Parse(args);
        return new RegistryOptions(
            values.Get("--rid") ?? "registry",
            values.Get("--http-url") ?? "http://127.0.0.1:0",
            values.Get("--log-dir") ?? "logs",
            values.Require("--registry-pub-endpoint"),
            values.Require("--registry-router-endpoint"));
    }
}