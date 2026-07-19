using RegistrationCodec.Server.JsonOnlyPeer.Infrastructure;
using Zlink.Framework.Contracts.Handlers;

namespace RegistrationCodec.Server.JsonOnlyPeer;

internal sealed class FirstFilter(EvidenceStore evidence) : IZLinkHandlerFilter
{
    public async ValueTask InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerFilterNext next,
        CancellationToken cancellationToken)
    {
        evidence.Add($"filter|name=first|phase=before|packet={invocation.PacketName}");
        await next();
        evidence.Add($"filter|name=first|phase=after|packet={invocation.PacketName}");
    }
}

internal sealed class SecondFilter(EvidenceStore evidence) : IZLinkHandlerFilter
{
    public async ValueTask InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerFilterNext next,
        CancellationToken cancellationToken)
    {
        evidence.Add($"filter|name=second|phase=before|packet={invocation.PacketName}");
        await next();
        evidence.Add($"filter|name=second|phase=after|packet={invocation.PacketName}");
    }
}
