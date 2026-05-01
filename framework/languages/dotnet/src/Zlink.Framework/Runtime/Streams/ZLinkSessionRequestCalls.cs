namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionRequestCall<TRequest>(
    ZLinkSessionContext context,
    TRequest request) : IZLinkSessionRequestCall
{
    private static readonly ZlinkStreamPacketNameResolver PacketNameResolver = new();

    private string? _packetName = PacketNameResolver.Resolve(typeof(TRequest));
    private TimeSpan _timeout = TimeSpan.FromSeconds(30);

    public IZLinkSessionRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSessionRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        return context.RequestClientAsync<TRequest, TReply>(
            request,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            _timeout,
            cancellationToken);
    }
}
