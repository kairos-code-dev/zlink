using Zlink.Framework.E2E.Configuration;
namespace SpotService.Client.Support;

internal sealed record ClientOptions(
    string GatewayUrl,
    string PlayAUrl,
    string PlayBUrl,
    string MultiAUrl,
    string MultiBUrl,
    string SessionAUrl,
    string SessionAStreamEndpoint,
    string SessionATlsStreamEndpoint,
    string SessionBStreamEndpoint,
    string OperationGroup)
{
    public static ClientOptions Parse(string[] args)
        => E2eConfiguration.Load<ClientOptions>(args);
}