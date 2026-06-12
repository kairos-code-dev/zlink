using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.Session;

internal sealed class DeliveryStatusFanoutHandler(CustomerSessionDirectory sessions)
    : IZLinkPublishHandler<DeliveryStatusNotify>
{
    public async ValueTask HandleAsync(
        DeliveryStatusNotify message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await sessions.PublishAsync(message, cancellationToken);
    }
}
