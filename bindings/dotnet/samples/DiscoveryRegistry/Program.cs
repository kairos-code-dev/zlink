using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
var registry = new Registry(ctx);
var provider = new PubSocket(ctx);
var discovery = new Discovery(ctx, ServiceType.Socket, "sample");
string registryPub = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string registryRouter = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string serviceEndpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";

registry.Bind(registryPub, registryRouter);
discovery.ConnectRegistry(registryRouter);
provider.AttachDiscovery(discovery);
provider.Bind(serviceEndpoint);

try
{
    SampleSupport.WaitOrThrow(
        () => Array.Exists(registry.TopologySnapshot(),
            entry => entry.ServiceName == "sample"),
        5000,
        "discovery registry sample");
    Console.WriteLine("[discovery-registry] service: \"sample\" -> discovered");
}
finally
{
    discovery.Dispose();
    registry.Dispose();
}
