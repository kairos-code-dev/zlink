namespace Zlink.Framework.Backend;

internal interface IZLinkMonitoringBackendAdapter
{
    IZLinkBackendSocketMonitor OpenSocketMonitor(IZLinkBackendSocket socket);
}
