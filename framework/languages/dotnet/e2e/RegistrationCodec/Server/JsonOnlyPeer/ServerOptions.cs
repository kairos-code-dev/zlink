namespace RegistrationCodec.Server.JsonOnlyPeer;

using Zlink.Framework.E2E.Configuration;

public sealed record ServerOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string? ChannelEndpoint,
    string? EvidenceFile,
    string? InvalidMode,
    string CodecMode,
    string? JsonOnlyPeerProject)
{
    public static ServerOptions Parse(string[] args)
        => E2eConfiguration.Load<ServerOptions>(args);
}
