namespace Zlink.Framework;

public interface IZLinkSendCall
{
    IZLinkSendCall WithPacketName(string packetName);

    IZLinkSendCall WithDontWait();

    bool Exec();
}

public interface IZLinkRequestCall<TReply>
{
    IZLinkRequestCall<TReply> WithPacketName(string packetName);

    IZLinkRequestCall<TReply> WithTimeout(TimeSpan timeout);

    ValueTask<TReply> ExecAsync(CancellationToken cancellationToken = default);
}

public interface IZLinkPublishCall
{
    IZLinkPublishCall WithPacketName(string packetName);

    IZLinkPublishCall WithDontWait();

    bool Exec();
}
