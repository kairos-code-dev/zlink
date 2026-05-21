namespace Zlink.Framework.Runtime.Handlers;

internal enum ZLinkHandlerArgumentKind
{
    Message,
    Context,
    CancellationToken,
    Default
}

internal sealed record ZLinkHandlerEndpointDescriptor(
    ZLinkMessageKind Kind,
    string MessageName,
    Type DeclaringType,
    ZLinkHandlerMethodInvoker Invoker,
    IReadOnlyList<ZLinkHandlerArgumentKind> ArgumentPlan,
    Type MessageType,
    Type? ReplyType,
    Type? ContextType,
    bool HasCancellationToken,
    IReadOnlySet<string> Groups,
    string? ExplicitChannelName);

internal readonly record struct ZLinkHandlerSelectionKey(
    ZLinkMessageKind Kind,
    string ChannelName,
    string MessageName);
