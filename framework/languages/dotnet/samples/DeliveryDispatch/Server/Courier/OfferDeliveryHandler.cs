using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.Courier;

internal sealed record CourierOptions(
    string CourierId,
    string Mode);

internal sealed class OfferDeliveryHandler(CourierOptions options)
    : IZLinkRequestHandler<OfferDelivery, OfferDeliveryResult>
{
    public async ValueTask<OfferDeliveryResult> HandleAsync(
        OfferDelivery request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (ShouldTimeout(request.DeliveryId))
        {
            Console.Error.WriteLine($"deliverydispatch courier: no response delivery={request.DeliveryId} courier={options.CourierId}");
            await Task.Delay(TimeSpan.FromSeconds(30), cancellationToken);
        }

        var accepted = !string.Equals(options.Mode, "reject", StringComparison.OrdinalIgnoreCase);
        Console.Error.WriteLine($"deliverydispatch courier: decision delivery={request.DeliveryId} courier={options.CourierId} accepted={accepted}");
        return new OfferDeliveryResult(
            request.DeliveryId,
            options.CourierId,
            accepted,
            accepted ? null : "courier rejected");
    }

    private bool ShouldTimeout(string deliveryId)
    {
        if (string.Equals(options.Mode, "timeout", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return string.Equals(options.Mode, "timeout-reassign", StringComparison.OrdinalIgnoreCase)
               && deliveryId.Contains("reassign", StringComparison.OrdinalIgnoreCase);
    }
}
