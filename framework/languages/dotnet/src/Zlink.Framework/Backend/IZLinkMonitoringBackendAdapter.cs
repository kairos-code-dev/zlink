namespace Zlink.Framework.Backend;

internal interface IZLinkMonitoringBackendAdapter
{
    IZLinkBackendSocketMonitor OpenSocketMonitor(IZLinkBackendSocket socket);

    IZLinkBackendServiceMonitor OpenDiscoveryMonitor(IZLinkBackendDiscovery discovery);
}
