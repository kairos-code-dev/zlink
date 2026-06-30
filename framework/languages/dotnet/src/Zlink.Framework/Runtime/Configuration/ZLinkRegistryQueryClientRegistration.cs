namespace Zlink.Framework.Runtime.Configuration;

internal sealed class ZLinkRegistryQueryClientRegistration
{
    public string? Endpoint { get; set; }
}

internal sealed class ZLinkRegistryQueryClientOptionsModel(
    ZLinkRegistryQueryClientRegistration registration) : IZLinkRegistryQueryClientOptions
{
    public string Endpoint
    {
        get => registration.Endpoint ?? string.Empty;
        set => registration.Endpoint = value;
    }
}

internal static class ZLinkRegistryQueryClientRegistrationValidator
{
    public static void Validate(ZLinkRegistryQueryClientRegistration registration)
    {
        ArgumentNullException.ThrowIfNull(registration);

        if (string.IsNullOrWhiteSpace(registration.Endpoint))
            throw new ZLinkConfigurationException(
                "Registry query client requires a non-empty endpoint.");
    }
}