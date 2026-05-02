using SampleCommon;
using Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
var registry = new Registry(ctx);
var query = new RegistryQueryClient(ctx);
var provider = new PubSocket(ctx);
var discovery = new Discovery(ctx, AutoConnectType.Fanout, "sample");
string registryPub = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string registryRouter = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";
string serviceEndpoint = $"tcp://127.0.0.1:{SampleSupport.ReservePort()}";

registry.Bind(registryPub, registryRouter);
discovery.ConnectRegistry(registryRouter);
provider.AttachDiscovery(discovery);
provider.Bind(serviceEndpoint);
query.Connect(registryRouter);

try
{
    SampleSupport.WaitOrThrow(
        () => Array.Exists(query.Snapshot(),
            entry => entry.ChannelName == "sample"),
        5000,
        "registry query sample");
    Console.WriteLine("[registry-query] service: \"sample\" -> snapshot: found");
}
finally
{
    discovery.Dispose();
    query.Dispose();
    registry.Dispose();
}
