using RegistrationCodec.Server.Infrastructure;
using RegistrationCodec.Server.InvalidDuplicate.Infrastructure;
using RegistrationCodec.Shared;
using Zlink.Framework.Contracts.Handlers;

namespace RegistrationCodec.Server.InvalidDuplicate.Handlers;

[ZLinkHandlerGroup("auto")]
internal sealed class EchoAutoRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<EchoAutoReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(EchoAutoReq request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-request|variant=auto|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}

[ZLinkHandlerGroup("auto")]
internal sealed class EchoAutoCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<EchoAutoCommand>
{
    public ValueTask HandleAsync(EchoAutoCommand message, ZLinkSendContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-command|variant=auto|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

[ZLinkHandlerGroup("attr")]
internal sealed class AttributeHandlers(EvidenceStore evidence)
{
    [ZLinkRequest(PacketName = "EchoAttr")]
    public EchoReply Request(EchoReq request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-request|variant=attr|value={request.Value}|content={context.ContentType}");
        return new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>");
    }

    [ZLinkSend(PacketName = "EchoAttrCommand")]
    public ValueTask Send(EchoCommand message, ZLinkSendContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-command|variant=attr|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EchoManualRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<EchoManualReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(EchoManualReq request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-request|variant=manual|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}

internal sealed class EchoManualCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<EchoManualCommand>
{
    public ValueTask HandleAsync(EchoManualCommand message, ZLinkSendContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"echo-command|variant=manual|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class DuplicateEchoRequestHandler
    : IZLinkRequestHandler<EchoManualReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(EchoManualReq request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new EchoReply(request.Value, "duplicate"));
    }
}
