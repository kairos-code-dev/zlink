namespace Zlink.Framework.Handlers;

public sealed class ZLinkHandlerInvocation
{
    internal ZLinkHandlerInvocation(
        object? message,
        IZLinkHandlerContext context,
        string? channelName,
        string? packetName,
        IServiceProvider services)
    {
        Message = message;
        Context = context;
        ChannelName = channelName;
        PacketName = packetName;
        Services = services;
    }

    public object? Message { get; }

    public IZLinkHandlerContext Context { get; }

    public string? ChannelName { get; }

    public string? PacketName { get; }

    internal IServiceProvider Services { get; }
}
