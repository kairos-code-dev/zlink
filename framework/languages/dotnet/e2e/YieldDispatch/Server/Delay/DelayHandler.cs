using YieldDispatch.Delay.Support;
using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Handlers;

namespace YieldDispatch.Server.Delay;

internal sealed class DelayHandler(NodeOptions node, EvidenceStore evidence)
    : IZLinkRequestHandler<DelayReq, DelayReply>
{
    public async ValueTask<DelayReply> HandleAsync(
        DelayReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        evidence.Add($"delay-started|rid={node.Rid}|request={request.RequestId}|marker={request.Marker}");
        await Task.Delay(TimeSpan.FromMilliseconds(request.DelayMs), cancellationToken);
        evidence.Add($"delay-completed|rid={node.Rid}|request={request.RequestId}|marker={request.Marker}");
        return new DelayReply(request.RequestId, request.Marker, node.Rid);
    }
}
