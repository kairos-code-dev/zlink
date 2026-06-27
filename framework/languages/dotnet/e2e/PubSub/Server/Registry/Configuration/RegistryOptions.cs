using PubSub.Server.Registry.Configuration;
using PubSub.Server.Registry;

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
            Rid: values.Get("--rid") ?? "registry",
            HttpUrl: values.Get("--http-url") ?? "http://127.0.0.1:0",
            LogDir: values.Get("--log-dir") ?? "logs",
            RegistryPubEndpoint: values.Require("--registry-pub-endpoint"),
            RegistryRouterEndpoint: values.Require("--registry-router-endpoint"));
    }
}
