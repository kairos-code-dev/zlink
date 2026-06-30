namespace Zlink.Framework.Contracts.Handlers;

public sealed class ZLinkHandlerInvocation
{
    internal ZLinkHandlerInvocation(
        object? message,
        IZLinkHandlerContext context,
        string? channelName,
        string? packetName)
    {
        Message = message;
        Context = context;
        ChannelName = channelName;
        PacketName = packetName;
    }

    public object? Message { get; }

    public IZLinkHandlerContext Context { get; }

    public string? ChannelName { get; }

    public string? PacketName { get; }
}