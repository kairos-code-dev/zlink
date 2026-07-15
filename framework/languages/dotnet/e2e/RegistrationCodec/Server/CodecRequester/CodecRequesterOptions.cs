namespace RegistrationCodec.Server.CodecRequester;

using Zlink.Framework.E2E.Configuration;

internal sealed record CodecRequesterOptions(
    string Rid,
    string HttpUrl,
    string ChannelEndpoint,
    string LogDir)
{
    public static CodecRequesterOptions Parse(string[] args)
        => E2eConfiguration.Load<CodecRequesterOptions>(args);
}
