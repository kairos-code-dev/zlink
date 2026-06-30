namespace Zlink.Framework.Contracts.Registry;

public interface IZLinkRegistryOptions
{
    string PubEndpoint { get; set; }

    string RouterEndpoint { get; set; }

    uint RegistryId { get; set; }

    TimeSpan HeartbeatInterval { get; set; }

    TimeSpan HeartbeatTimeout { get; set; }

    TimeSpan BroadcastInterval { get; set; }

    void AddPeer(string peerPubEndpoint);
}