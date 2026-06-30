namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendPublisherSocketWrapper(IPubSocket nativeSocket) : IZLinkBackendPublisherSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        nativeSocket.SetRoutingId(routingId);
    }

    public void SetSendHighWaterMark(int value)
    {
        nativeSocket.Options.SendHighWaterMark = value;
    }

    public void SetReceiveHighWaterMark(int value)
    {
        nativeSocket.Options.ReceiveHighWaterMark = value;
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<IDiscovery>());
    }

    public void OnSendReady(Action handler)
    {
        nativeSocket.OnSendReady(handler);
    }

    public bool Publish(
        string topic,
        Message message,
        SendFlags flags)
    {
        return nativeSocket.Publish(topic)
            .Message(message)
            .Flags(flags)
            .Submit();
    }

    public bool Publish(
        string topic,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSocket.Publish(topic)
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public ValueTask DisposeAsync()
    {
        return nativeSocket.DisposeAsync();
    }
}