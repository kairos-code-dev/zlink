using PubSub.Server.Infrastructure;
using PubSub.Shared;
using Zlink.Framework.Contracts.Handlers;

namespace PubSub.Server;

internal sealed class EventNotifyHandler(EvidenceStore evidence, HandlerDelayOptions delayOptions)
    : IZLinkPublishHandler<EventNotify>
{
    public async ValueTask HandleAsync(
        EventNotify message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (delayOptions.DelayMs > 0 && message.Value.StartsWith("slow-", StringComparison.Ordinal))
        {
            evidence.Add(
                $"delay-start|rid={evidence.Rid}|run={message.RunId}|topic={context.Topic}"
                + $"|seq={message.Sequence}|value={message.Value}");
            await Task.Delay(delayOptions.DelayMs, cancellationToken);
        }

        if (context.Topic == PubSubNames.MainTopic)
        {
            evidence.Add(
                $"event|rid={evidence.Rid}|run={message.RunId}|topic={context.Topic}"
                + $"|seq={message.Sequence}|value={message.Value}|packet={context.PacketName}");
        }
        else
        {
            evidence.Add(
                $"ignored|rid={evidence.Rid}|run={message.RunId}|topic={context.Topic}"
                + $"|seq={message.Sequence}|value={message.Value}|packet={context.PacketName}");
        }
    }
}
