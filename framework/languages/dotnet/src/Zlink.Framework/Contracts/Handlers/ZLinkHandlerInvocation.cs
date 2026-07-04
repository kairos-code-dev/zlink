namespace Zlink.Framework.Contracts.Handlers;

public sealed class ZLinkHandlerInvocation
{
    internal ZLinkHandlerInvocation(
        object? message,
        IZLinkHandlerContext context)
    {
        Message = message;
        Context = context;
    }

    public object? Message { get; }

    public IZLinkHandlerContext Context { get; }

    public string? ChannelName => Context.ChannelName;

    public string? PacketName => Context.PacketName;
}
