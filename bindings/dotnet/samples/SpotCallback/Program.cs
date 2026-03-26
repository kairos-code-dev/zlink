using SampleCommon;
using Zlink;
using Zlink.Service;

if (!SampleSupport.IsNativeAvailable())
    return;

using var ctx = new Context();
using var spot = new Spot(ctx);
spot.SetSubscription("spot:callback");
spot.SubscribeHandler((topic, parts) =>
{
    foreach (Message part in parts)
        part.Dispose();
});
spot.SendReadyHandler(() => { });
using ServiceMonitor monitor = spot.OpenMonitor();
monitor.Close();
Console.WriteLine("spot:callback");
