namespace Zlink.Framework.Streams;

public interface IZLinkSessionRequestCall
{
    IZLinkSessionRequestCall WithPacketName(string packetName);

    IZLinkSessionRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Submit<TReply>(CancellationToken cancellationToken = default);
}
