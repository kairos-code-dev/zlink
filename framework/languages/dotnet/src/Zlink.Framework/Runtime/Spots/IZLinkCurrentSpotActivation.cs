namespace Zlink.Framework.Runtime.Spots;

internal interface IZLinkCurrentSpotActivation
{
    string ChannelName { get; }

    TimeSpan DefaultRequestTimeout { get; }

    ZLinkCodecRegistryBuilder Codecs { get; }

    IZLinkSpotOutbound Outbound { get; }

    ZLinkSpotOutboundEndpoint OutboundEndpoint { get; }
}
