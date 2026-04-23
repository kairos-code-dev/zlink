namespace Zlink.Framework.Backend;

internal interface IZLinkChannelBackendAdapter
{
    IZLinkBackendContext CreateContext();

    IZLinkBackendDiscovery CreateDiscovery(
        IZLinkBackendContext context,
        ZLinkBackendServiceType serviceType,
        string serviceName);

    IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context);

    IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context);

    IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context);

    IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context);
}
