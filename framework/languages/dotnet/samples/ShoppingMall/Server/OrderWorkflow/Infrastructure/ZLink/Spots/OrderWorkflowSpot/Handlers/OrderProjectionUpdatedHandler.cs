using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMall.Server.OrderWorkflow.Infrastructure.ZLink.Spots.OrderWorkflowSpot.Handlers;

[ZLinkSpotSubscriptionHandler(SampleNames.OrderProjectionChannel, SampleNames.OrderProjectionTopic)]
internal sealed class OrderProjectionUpdatedHandler(
    ILogger<OrderProjectionUpdatedHandler> logger)
    : IZLinkSpotSubscriptionHandler<OrderWorkflowSpot, OrderProjectionUpdatedEvent>
{
    public ValueTask HandleAsync(
        OrderWorkflowSpot spot,
        OrderProjectionUpdatedEvent message,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "shoppingmall projection fanout: received. subscriber={SubscriberSpotRid}, order={OrderId}, status={Status}",
            spot.Context.SpotRid.ToString(),
            message.OrderId,
            message.Status);
        return ValueTask.CompletedTask;
    }
}
