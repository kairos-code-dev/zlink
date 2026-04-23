namespace Zlink.Framework;

public sealed record ZLinkHandlerInvocation(
    object? Message,
    IZLinkHandlerContext Context,
    string? ChannelName,
    string? PacketName,
    IServiceProvider Services);
