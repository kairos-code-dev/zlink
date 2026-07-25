namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotRelocationReplyRelayProtocol
{
    internal const string PacketName =
        "zlink.internal.spot.relocation.reply.v1";
}

[ZLinkPacket(ZLinkSpotRelocationReplyRelayProtocol.PacketName)]
internal sealed record ZLinkSpotRelocationReplyRelay(
    ulong ReplyRouteId,
    string SpotId,
    ulong ObjectGeneration,
    ulong TargetAuthorityOwnerGeneration,
    int HopCount,
    byte[][] Parts);

internal sealed class ZLinkSpotRelocationReplyRelayHandler(
    ZLinkFrameworkRuntime runtime)
    : IZLinkRouteSendHandler<ZLinkSpotRelocationReplyRelay>
{
    public ValueTask HandleAsync(
        ZLinkSpotRelocationReplyRelay message,
        ZLinkRouteMessageContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var parts = message.Parts
            .Select(static part => Message.From(part))
            .ToArray();
        var submitted = false;
        try
        {
            submitted = runtime.SpotRelocationReplyRoutes.TryRelay(
                            message.ReplyRouteId,
                            message.SpotId,
                            message.ObjectGeneration,
                            context.SourceNodeRid,
                            message.TargetAuthorityOwnerGeneration,
                            message.HopCount,
                            parts,
                            SendFlags.None)
                        == SubmitResult.Ok;
        }
        finally
        {
            if (!submitted)
                ZLinkMessageParts.DisposeAll(parts);
        }
        return ValueTask.CompletedTask;
    }
}
