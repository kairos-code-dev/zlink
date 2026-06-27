using Google.Protobuf.WellKnownTypes;
using RegistrationCodec.Server.Infrastructure;
using RegistrationCodec.Shared;
using Zlink.Framework.Contracts.Handlers;

namespace RegistrationCodec.Server.Handlers;

internal sealed class JsonEchoRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<JsonEchoReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(JsonEchoReq request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-request|codec=json|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}

internal sealed class JsonEchoCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<JsonEchoCommand>
{
    public ValueTask HandleAsync(JsonEchoCommand message, ZLinkSendContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-command|codec=json|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ProtobufEchoRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<StringValue, StringValue>
{
    public ValueTask<StringValue> HandleAsync(StringValue request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-request|codec=protobuf|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new StringValue { Value = $"echo:{request.Value}|content:{context.ContentType}" });
    }
}

internal sealed class ProtobufEchoCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<StringValue>
{
    public ValueTask HandleAsync(StringValue message, ZLinkSendContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-command|codec=protobuf|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class MessagePackEchoRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<PackedEchoReq, PackedEchoReq>
{
    public ValueTask<PackedEchoReq> HandleAsync(PackedEchoReq request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-request|codec=msgpack|value={request.Value}|content={context.ContentType}");
        return ValueTask.FromResult(new PackedEchoReq { Value = $"echo:{request.Value}|content:{context.ContentType}" });
    }
}

internal sealed class MessagePackEchoCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<PackedEchoCommand>
{
    public ValueTask HandleAsync(PackedEchoCommand message, ZLinkSendContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"codec-command|codec=msgpack|id={message.CommandId}|value={message.Value}|content={context.ContentType}");
        return ValueTask.CompletedTask;
    }
}
