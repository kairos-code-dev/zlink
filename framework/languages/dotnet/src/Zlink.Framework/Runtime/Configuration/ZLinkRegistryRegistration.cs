namespace Zlink.Framework.Runtime.Configuration;

internal sealed class ZLinkRegistryRegistration
{
    public string? PubEndpoint { get; set; }

    public string? RouterEndpoint { get; set; }

    public uint RegistryId { get; set; }

    public TimeSpan HeartbeatInterval { get; set; } = TimeSpan.FromSeconds(5);

    public TimeSpan HeartbeatTimeout { get; set; } = TimeSpan.FromSeconds(15);

    public TimeSpan BroadcastInterval { get; set; } = TimeSpan.FromSeconds(30);

    public List<string> PeerPubEndpoints { get; } = [];
}

internal sealed class ZLinkRegistryOptionsModel(ZLinkRegistryRegistration registration) : IZLinkRegistryOptions
{
    public string PubEndpoint
    {
        get => registration.PubEndpoint ?? string.Empty;
        set => registration.PubEndpoint = value;
    }

    public string RouterEndpoint
    {
        get => registration.RouterEndpoint ?? string.Empty;
        set => registration.RouterEndpoint = value;
    }

    public uint RegistryId
    {
        get => registration.RegistryId;
        set => registration.RegistryId = value;
    }

    public TimeSpan HeartbeatInterval
    {
        get => registration.HeartbeatInterval;
        set => registration.HeartbeatInterval = value;
    }

    public TimeSpan HeartbeatTimeout
    {
        get => registration.HeartbeatTimeout;
        set => registration.HeartbeatTimeout = value;
    }

    public TimeSpan BroadcastInterval
    {
        get => registration.BroadcastInterval;
        set => registration.BroadcastInterval = value;
    }

    public void AddPeer(string peerPubEndpoint)
    {
        registration.PeerPubEndpoints.Add(peerPubEndpoint);
    }
}

internal static class ZLinkRegistryRegistrationValidator
{
    public static void Validate(ZLinkRegistryRegistration registration)
    {
        ArgumentNullException.ThrowIfNull(registration);

        if (string.IsNullOrWhiteSpace(registration.PubEndpoint))
            throw new ZLinkConfigurationException(
                "Registry registration requires a non-empty pub endpoint.");

        if (string.IsNullOrWhiteSpace(registration.RouterEndpoint))
            throw new ZLinkConfigurationException(
                "Registry registration requires a non-empty router endpoint.");

        if (registration.HeartbeatInterval <= TimeSpan.Zero)
            throw new ZLinkConfigurationException(
                "Registry heartbeat interval must be greater than zero.");

        if (registration.HeartbeatTimeout <= TimeSpan.Zero)
            throw new ZLinkConfigurationException(
                "Registry heartbeat timeout must be greater than zero.");

        if (registration.BroadcastInterval <= TimeSpan.Zero)
            throw new ZLinkConfigurationException(
                "Registry broadcast interval must be greater than zero.");
    }
}