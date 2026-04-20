using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var registry = new Registry(ctx);
using var discovery = new Discovery(ctx, ServiceType.Socket, "sample");
using var provider = new PubSocket(ctx);
string registryPub = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string registryRouter = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string serviceEndpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";

registry.Bind(registryPub, registryRouter);
discovery.ConnectRegistry(registryRouter);
provider.AttachDiscovery(discovery);
provider.Bind(serviceEndpoint);

SampleSupport.WaitOrThrow(
    () => Array.Exists(registry.TopologySnapshot(),
        entry => entry.ServiceName == "sample"),
    5000,
    "discovery registry sample");
Console.WriteLine("[discovery-registry] service: \"sample\" -> discovered");
