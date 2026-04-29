namespace Zlink.Framework.Backend.Contracts;

internal interface IZLinkMonitoringBackendAdapter
{
    IZLinkBackendSocketMonitor OpenSocketMonitor(IZLinkBackendSocket socket);
}
