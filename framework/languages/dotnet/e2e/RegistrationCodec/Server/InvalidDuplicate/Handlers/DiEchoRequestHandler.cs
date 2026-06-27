using RegistrationCodec.Server.InvalidDuplicate.Infrastructure;
using RegistrationCodec.Shared;
using Zlink.Framework.Contracts.Handlers;
using RegistrationCodec.Server.InvalidDuplicate;

namespace RegistrationCodec.Server.InvalidDuplicate.Handlers;

internal sealed class DiEchoRequestHandler(
    EvidenceStore evidence,
    SingletonProbe singleton,
    ScopedProbe scoped)
    : IZLinkRequestHandler<EchoReq, EchoReply>
{
    public ValueTask<EchoReply> HandleAsync(EchoReq request, ZLinkRequestContext context, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"di|value={request.Value}|singleton={singleton.Id}|scoped={scoped.Id}|disposed={ScopedProbe.DisposedCount}");
        return ValueTask.FromResult(new EchoReply($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}
