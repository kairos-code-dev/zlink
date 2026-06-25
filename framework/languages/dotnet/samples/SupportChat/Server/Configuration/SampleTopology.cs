using Systems.Zlink;

namespace SupportChat.Server.Configuration;

public sealed record SampleTopology(
    string RegistryPubEndpoint,
    string RegistryRouterEndpoint,
    string ApiChannelEndpoint,
    string SupportChannelEndpoint,
    string SessionSpotEndpoint,
    string SessionRouterEndpoint,
    string SupportRouterEndpoint,
    string SupportEntrySpotEndpoint,
    string SupportEntrySpotRouterEndpoint,
    string SupportConversationSpotEndpoint,
    string SupportConversationSpotRouterEndpoint,
    string StreamEndpoint,
    string ReconnectStreamEndpoint,
    RoutingId SessionRouterRid,
    RoutingId SessionPubRid,
    RoutingId SupportEntryRid)
{
    public SampleSessionNode PrimarySession => new(
        SessionSpotEndpoint,
        SessionRouterEndpoint,
        StreamEndpoint,
        SessionRouterRid,
        SessionPubRid);

    public static SampleTopology Create()
    {
        return new SampleTopology(
            ReadEndpoint("SUPPORTCHAT_REGISTRY_PUB_ENDPOINT", "tcp://127.0.0.1:47201"),
            ReadEndpoint("SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT", "tcp://127.0.0.1:47202"),
            ReadEndpoint("SUPPORTCHAT_API_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47203"),
            ReadEndpoint("SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47204"),
            ReadEndpoint("SUPPORTCHAT_SESSION_SPOT_ENDPOINT", "tcp://127.0.0.1:47205"),
            ReadEndpoint("SUPPORTCHAT_SESSION_ROUTER_ENDPOINT", "tcp://127.0.0.1:47206"),
            ReadEndpoint("SUPPORTCHAT_SUPPORT_ROUTER_ENDPOINT", "tcp://127.0.0.1:47207"),
            ReadEndpoint("SUPPORTCHAT_ENTRY_SPOT_ENDPOINT", "tcp://127.0.0.1:47208"),
            ReadEndpoint("SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:47209"),
            ReadEndpoint("SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT", "tcp://127.0.0.1:47210"),
            ReadEndpoint("SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:47211"),
            ReadEndpoint("SUPPORTCHAT_STREAM_ENDPOINT", "tcp://127.0.0.1:47212"),
            ReadEndpoint("SUPPORTCHAT_RECONNECT_STREAM_ENDPOINT", "tcp://127.0.0.1:47213"),
            RoutingId.From("3101"),
            RoutingId.From("3102"),
            RoutingId.From("4201"));
    }

    private static string ReadEndpoint(string name, string defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? defaultValue : value;
    }
}

public sealed record SampleSessionNode(
    string PubEndpoint,
    string RouterEndpoint,
    string StreamEndpoint,
    RoutingId RoutingId,
    RoutingId PublisherRoutingId);
