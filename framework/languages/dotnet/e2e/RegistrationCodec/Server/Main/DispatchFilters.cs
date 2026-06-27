using RegistrationCodec.Server.Main.Infrastructure;
using Zlink.Framework.Contracts.Handlers;
using RegistrationCodec.Server.Main.Endpoints;
using RegistrationCodec.Server.Main.Handlers;

namespace RegistrationCodec.Server.Main;

internal sealed class FirstFilter(EvidenceStore evidence) : IZLinkHandlerFilter
{
    public async ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerDelegate next,
        CancellationToken cancellationToken)
    {
        evidence.Add($"filter|name=first|phase=before|packet={invocation.PacketName}");
        var result = await next(cancellationToken);
        evidence.Add($"filter|name=first|phase=after|packet={invocation.PacketName}");
        return result;
    }
}

internal sealed class SecondFilter(EvidenceStore evidence) : IZLinkHandlerFilter
{
    public async ValueTask<object?> InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerDelegate next,
        CancellationToken cancellationToken)
    {
        evidence.Add($"filter|name=second|phase=before|packet={invocation.PacketName}");
        var result = await next(cancellationToken);
        evidence.Add($"filter|name=second|phase=after|packet={invocation.PacketName}");
        return result;
    }
}
