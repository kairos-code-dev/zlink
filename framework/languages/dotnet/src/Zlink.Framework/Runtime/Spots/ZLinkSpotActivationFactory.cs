using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActivationFactory(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration frameworkRegistration,
    ZLinkSpotNodeRegistration registration,
    IZLinkBackendSpotNode node,
    string spotChannelName,
    Func<string, ZLinkAsyncSubmitter?> channelSubmitter,
    Action connectDiscoveredPubSubPeers)
{
    public async ValueTask<ZLinkSpotActivationCreateResult> CreateAsync(
        Type spotType,
        IZLinkBackendSpot nativeSpot,
        Message request,
        CancellationToken cancellationToken)
    {
        connectDiscoveredPubSubPeers();
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
                frameworkRegistration.DefaultTimeout,
                registration.Router?.SocketConfig.SendTimeout
                    ?? TimeSpan.FromMilliseconds(200),
                channelSubmitter);

            var spot = (IZLinkSpot)ActivatorUtilities.CreateInstance(
                spotScope.ServiceProvider,
                spotType,
                activation);

            activation.AttachSpot(spot);
            spot.Configure();
            activation.BindDescriptors();
            var response = await activation.InitializeAsync(request, cancellationToken).ConfigureAwait(false);
            return new ZLinkSpotActivationCreateResult(activation, response);
        }
        catch
        {
            if (activation is null)
            {
                await spotScope.DisposeAsync().ConfigureAwait(false);
            }
            else
            {
                await activation.DisposeAsync().ConfigureAwait(false);
            }

            throw;
        }
    }
}

internal readonly record struct ZLinkSpotActivationCreateResult(
    ZLinkSpotActivation Activation,
    ZLinkSpotCreateResponse Response);
