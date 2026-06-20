using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Dispatch;

namespace TicTacToe.Server.Configuration;

internal sealed class TicTacToeDispatchErrorObserver(
    ILogger<TicTacToeDispatchErrorObserver> logger) : IZLinkMessageDispatchErrorObserver
{
    public ValueTask OnDispatchErrorAsync(
        ZLinkMessageDispatchErrorEvent error,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogError(
            error.Exception,
            "tictactoe dispatch-error surface={Surface} messageKind={MessageKind} reason={Reason} action={Action} packetName={PacketName} channelName={ChannelName} topic={Topic} spotRid={SpotRid} actorId={ActorId} sourceRid={SourceRid} correlationId={CorrelationId}",
            error.Surface,
            error.MessageKind,
            error.Reason,
            error.Action,
            error.PacketName,
            error.ChannelName,
            error.Topic,
            error.SpotRid,
            error.ActorId,
            error.SourceRid,
            error.CorrelationId);
        return ValueTask.CompletedTask;
    }
}
