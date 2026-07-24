namespace Zlink.Framework.Runtime.Spots;

internal interface IZLinkCurrentSpotActivation
{
    string ChannelName { get; }

    string SpotId { get; }

    TimeSpan DefaultRequestTimeout { get; }

    ZLinkCodecRegistryBuilder Codecs { get; }

    ZLinkMessageFlowTracer Flow { get; }

    IZLinkRuntimeErrorSink ErrorSink { get; }

    IZLinkSpotOutbound Outbound { get; }

    ZLinkSpotOutboundEndpoint OutboundEndpoint { get; }
}
