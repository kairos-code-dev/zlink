using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActivationFactory(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration frameworkRegistration,
    ZLinkSpotNodeRegistration registration,
    IZLinkBackendSpotNode node,
    string spotChannelName)
{
    public async ValueTask<ZLinkSpotActivationCreateResult> CreateAsync(
        Type spotType,
        IZLinkBackendSpot nativeSpot,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var spotScope = services.CreateAsyncScope();
        ZLinkSpotActivation? activation = null;
        try
        {
            activation = new ZLinkSpotActivation(
                runtime,
                spotScope,
                nativeSpot,
                node.RoutingId,
                registration.SpotNodeName,
                spotChannelName,
                frameworkRegistration.DefaultRequestTimeout,
                registration.Router?.SocketConfig.SendTimeout
                ?? frameworkRegistration.DefaultSocketSendTimeout);

            var spot = (IZLinkSpot)ActivatorUtilities.CreateInstance(
                spotScope.ServiceProvider,
                spotType,
                activation);

            activation.AttachSpot(spot);
            foreach (var assembly in frameworkRegistration.HandlerAssemblies)
            foreach (var handler in ZLinkScannedSpotHandlerScanner.Scan(assembly))
                await activation.ApplyScannedHandlerAsync(handler, cancellationToken)
                    .ConfigureAwait(false);

            spot.Configure();
            activation.BindDescriptors();
            var response = await activation.InitializeAsync(request, cancellationToken).ConfigureAwait(false);
            return new ZLinkSpotActivationCreateResult(activation, response);
        }
        catch
        {
            if (activation is null)
                await spotScope.DisposeAsync().ConfigureAwait(false);
            else
                await activation.DisposeAsync().ConfigureAwait(false);

            throw;
        }
    }
}

internal readonly record struct ZLinkSpotActivationCreateResult(
    ZLinkSpotActivation Activation,
    ZLinkSpotCreateResponse Response);