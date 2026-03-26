using SampleCommon;
using Zlink;
using Zlink.Service;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var registry = new Registry(ctx);
using var discovery = new Discovery(ctx, ServiceType.Socket, "registry-sample");

RegistryStatus snapshot = registry.Snapshot();
using ServiceMonitor monitor = discovery.OpenMonitor();
monitor.Close();
Console.WriteLine($"topology:{snapshot.TopologyEntryCount}");
