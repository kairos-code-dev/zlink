namespace Zlink.Framework.Handlers;

public sealed record ZLinkHandlerInvocation(
    object? Message,
    IZLinkHandlerContext Context,
    string? ChannelName,
    string? MessageName,
    IServiceProvider Services);
