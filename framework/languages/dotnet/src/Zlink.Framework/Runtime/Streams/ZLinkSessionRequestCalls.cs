namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionRequestCall<TRequest>(
    ZLinkSessionContext context,
    TRequest request,
    TimeSpan defaultTimeout) : IZLinkSessionRequestCall
{
    private static readonly IZlinkStreamPacketNameResolver PacketNameResolver = ZlinkStreamDefaultCodecs.PacketNameResolver();

    private string? _packetName = PacketNameResolver.Resolve(typeof(TRequest));
    private TimeSpan? _timeout;

    public IZLinkSessionRequestCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkSessionRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        return context.RequestClientAsync<TRequest, TReply>(
            request,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            _timeout ?? defaultTimeout,
            cancellationToken);
    }
}
