namespace Zlink.Framework.Handlers;

public sealed record ZLinkHandlerInvocation(
    object? Message,
    IZLinkHandlerContext Context,
    string? ChannelName,
    string? PacketName,
    IServiceProvider Services);
