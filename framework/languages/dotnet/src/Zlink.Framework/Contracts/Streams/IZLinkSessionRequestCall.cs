namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkSessionRequestCall
{
    IZLinkSessionRequestCall PacketName(string packetName);

    IZLinkSessionRequestCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
}
