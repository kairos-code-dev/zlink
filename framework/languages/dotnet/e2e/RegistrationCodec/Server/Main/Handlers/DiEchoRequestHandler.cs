using RegistrationCodec.Server.Main.Infrastructure;
using RegistrationCodec.Shared;
using Zlink.Framework.Contracts.Handlers;

namespace RegistrationCodec.Server.Main.Handlers;

internal sealed class DiEchoRequestHandler(
    EvidenceStore evidence,
    SingletonProbe singleton,
    ScopedProbe scoped)
    : IZLinkRequestHandler<EchoReq, EchoRes>
{
    public ValueTask<EchoRes> HandleAsync(EchoReq request, ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"di|value={request.Value}|singleton={singleton.Id}|scoped={scoped.Id}|disposed={ScopedProbe.DisposedCount}");
        return ValueTask.FromResult(new EchoRes($"echo:{request.Value}", context.ContentType ?? "<null>"));
    }
}