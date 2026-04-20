using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var registry = new Registry(ctx);
using var discovery = new Discovery(ctx, ServiceType.Socket, "sample");
using var query = new RegistryQueryClient(ctx);
using var provider = new PubSocket(ctx);
string registryPub = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string registryRouter = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string serviceEndpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";

registry.Bind(registryPub, registryRouter);
discovery.ConnectRegistry(registryRouter);
provider.AttachDiscovery(discovery);
provider.Bind(serviceEndpoint);
query.Connect(registryRouter);

SampleSupport.WaitOrThrow(
    () => Array.Exists(query.Snapshot(),
        entry => entry.ServiceName == "sample"),
    5000,
    "registry query sample");
Console.WriteLine("[registry-query] service: \"sample\" -> snapshot: found");
