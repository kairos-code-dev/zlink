using SampleCommon;
using Systems.Zlink;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = Zlink.CreateContext();
var registry = ctx.CreateRegistry();
var query = ctx.CreateRegistryQueryClient();
var provider = ctx.CreatePubSocket();
var discovery = ctx.CreateDiscovery(AutoConnectType.Fanout, "sample");
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
