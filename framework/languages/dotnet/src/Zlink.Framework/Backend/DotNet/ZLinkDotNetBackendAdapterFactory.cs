namespace Zlink.Framework.Backend.DotNet;


internal sealed class ZLinkDotNetBackendAdapterFactory : IZLinkBackendAdapterFactory
{
    private static readonly IZLinkChannelBackendAdapter ChannelAdapter = new ZLinkDotNetChannelBackendAdapter();
    private static readonly IZLinkSpotBackendAdapter SpotAdapter = new ZLinkDotNetSpotBackendAdapter();
    private static readonly IZLinkStreamBackendAdapter StreamAdapter = new ZLinkDotNetStreamBackendAdapter();
    private static readonly IZLinkRegistryBackendAdapter RegistryAdapter = new ZLinkDotNetRegistryBackendAdapter();
    private static readonly IZLinkMonitoringBackendAdapter MonitoringAdapter = new ZLinkDotNetMonitoringBackendAdapter();

    public IZLinkChannelBackendAdapter CreateChannelAdapter() => ChannelAdapter;

    public IZLinkSpotBackendAdapter CreateSpotAdapter() => SpotAdapter;

    public IZLinkStreamBackendAdapter CreateStreamAdapter() => StreamAdapter;

    public IZLinkRegistryBackendAdapter CreateRegistryAdapter() => RegistryAdapter;

    public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => MonitoringAdapter;
}
