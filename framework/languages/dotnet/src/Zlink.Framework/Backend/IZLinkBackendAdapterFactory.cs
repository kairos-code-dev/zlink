namespace Zlink.Framework.Backend;

internal interface IZLinkBackendAdapterFactory
{
    IZLinkChannelBackendAdapter CreateChannelAdapter();

    IZLinkSpotBackendAdapter CreateSpotAdapter();

    IZLinkStreamBackendAdapter CreateStreamAdapter();

    IZLinkRegistryBackendAdapter CreateRegistryAdapter();

    IZLinkMonitoringBackendAdapter CreateMonitoringAdapter();
}
