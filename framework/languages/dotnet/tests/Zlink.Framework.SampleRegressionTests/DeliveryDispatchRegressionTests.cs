using Xunit;

namespace Zlink.Framework.SampleRegressionTests;

public sealed partial class RegressionTests
{
    [Fact]
    public void DeliveryDispatch_Runner_Reuses_External_Redis_And_Uses_Location_Store()
    {
        var sampleRoot = ResolveSampleRoot("DeliveryDispatch");
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.ko.md"));
        var topology = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleTopology.cs"));

        Assert.Contains("if [[ -z \"${DELIVERYDISPATCH_REDIS_ENDPOINT:-}\" ]]", shellRunner,
            StringComparison.Ordinal);
        AssertShellRunnerUsesRedisDockerHelper(
            shellRunner,
            "deliverydispatch-dotnet-redis",
            "DELIVERYDISPATCH_REDIS_ENDPOINT");
        Assert.Contains("DELIVERYDISPATCH_REDIS_KEY_PREFIX", shellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("topology=ready", shellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-reassignment=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-server-evidence=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-runner-evidence=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("grep -Rq \"message flow\" \"${DELIVERYDISPATCH_LOG_DIR}\"", shellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_STARTUP_DELAY_SECONDS", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_STARTUP_SETTLE_SECONDS", shellRunner, StringComparison.Ordinal);

        Assert.Contains("if (-not $env:DELIVERYDISPATCH_REDIS_ENDPOINT)", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name $RedisContainer", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("if ($RedisContainer)", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$env:DELIVERYDISPATCH_LOG_DIR = $SampleLogDir", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("DELIVERYDISPATCH_REDIS_KEY_PREFIX", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("topology=ready", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-reassignment=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-server-evidence=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-runner-evidence=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Wait-SampleLogContains \"message flow\" \"DeliveryDispatch message-flow evidence\"",
            powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Select-String -Pattern $Pattern -List", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_STARTUP_DELAY_SECONDS", powershellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_STARTUP_SETTLE_SECONDS", powershellRunner,
            StringComparison.Ordinal);

        Assert.Contains("DELIVERYDISPATCH_REDIS_ENDPOINT", topology, StringComparison.Ordinal);
        Assert.Contains("DELIVERYDISPATCH_REDIS_KEY_PREFIX", topology, StringComparison.Ordinal);
        foreach (var hostFactory in Directory.EnumerateFiles(Path.Combine(sampleRoot, "Server"), "*HostFactory.cs",
                     SearchOption.AllDirectories))
        {
            AssertLocationStoreHost(File.ReadAllText(hostFactory));
        }

        Assert.Contains("`DELIVERYDISPATCH_REDIS_ENDPOINT`가 이미 있으면", readme, StringComparison.Ordinal);
        Assert.Contains("그 Redis를 재사용하고 정리하지 않는다", readme, StringComparison.Ordinal);
        Assert.Contains("`run_sample.sh`와 `run_sample.ps1`", readme, StringComparison.Ordinal);
        Assert.Contains("AssignDeliveryMsg", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("`AssignDelivery`", readme, StringComparison.Ordinal);
    }

    [Fact]
    public void DeliveryDispatch_Contracts_Match_Common_Role_Model()
    {
        var sampleRoot = ResolveSampleRoot("DeliveryDispatch");
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.ko.md"));
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var topology = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleTopology.cs"));
        var messages = File.ReadAllText(Path.Combine(sampleRoot, "Shared", "Contracts", "Messages.cs"));
        var courierRoutes = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CourierActorNode", "RouteHandlers.cs"));
        var customerSession = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CustomerGateway",
            "SubscribeDeliverySessionHandler.cs"));
        var customerStatusHandler = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CustomerGateway",
            "Spots", "EntrySpot", "Handlers", "DeliveryStatusUpdatedHandler.cs"));
        var customerAccess = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CustomerGateway",
            "CustomerActorAccess.cs"));
        var clientScenario = File.ReadAllText(Path.Combine(sampleRoot, "Client", "DeliveryDispatchClientScenario.cs"));

        Assert.Contains("record AssignDeliveryMsg", messages, StringComparison.Ordinal);
        Assert.Contains("record FindCourierActorReq", messages, StringComparison.Ordinal);
        Assert.Contains("record FindCustomerActorReq", messages, StringComparison.Ordinal);
        Assert.Contains("record DeliveryStatusUpdatedMsg", messages, StringComparison.Ordinal);
        Assert.Contains("record BindCourierSessionReq", messages, StringComparison.Ordinal);
        Assert.Contains("record BindCourierSessionRes", messages, StringComparison.Ordinal);
        Assert.Contains("ActorRefSnapshot? Actor = null", messages, StringComparison.Ordinal);
        Assert.Contains("string? SessionRoute = null", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("record DeliveryStatusUpdatedRes", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("record BindCourierReq", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("record ReassignDelivery", messages, StringComparison.Ordinal);
        Assert.Contains("FindAsync(request.CourierId", courierRoutes, StringComparison.Ordinal);
        Assert.Contains("FindAsync(request.CustomerId", customerAccess, StringComparison.Ordinal);
        Assert.Contains("GetOrCreateAsync(", customerAccess, StringComparison.Ordinal);
        Assert.Contains("new FindCustomerActorReq(CustomerId)", customerSession, StringComparison.Ordinal);
        Assert.Contains("new EnsureCustomerActorReq(CustomerId)", customerSession, StringComparison.Ordinal);
        Assert.Contains("IZLinkSpotPacketHandler<CustomerEntrySpot, DeliveryStatusUpdatedMsg>",
            customerStatusHandler, StringComparison.Ordinal);
        Assert.Contains(".Actor.NodeRid", clientScenario, StringComparison.Ordinal);
        Assert.Contains("WaitForStatusAsync(customer, deliveryId, DeliveryStatus.Assigned", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("WaitForStatusAsync(customer, deliveryId, DeliveryStatus.Reassigned", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("deliverydispatch courier-session: bound courier=courier-a", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("deliverydispatch courier-session: bound courier=courier-b", shellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("CourierGateway", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("deliverydispatch.courier", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("courier-gateway", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("courier-gateway", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_COURIER_ROUTE", topology, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_CUSTOMER_ROUTE", topology, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_COURIER_ROUTE", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_CUSTOMER_ROUTE", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_COURIER_ROUTE", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_CUSTOMER_ROUTE", powershellRunner, StringComparison.Ordinal);
        Assert.False(Directory.Exists(Path.Combine(sampleRoot, "Server", "CourierGateway")));
    }

    [Fact]
    public void DeliveryDispatch_CourierSession_Bind_Registers_Binder_And_Replies_To_Client()
    {
        var sampleRoot = ResolveSampleRoot("DeliveryDispatch");
        var courierSessionBinder = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CourierSession",
            "CourierSessionBinder.cs"));
        var courierSessionHandler = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CourierSession",
            "BindCourierSessionHandler.cs"));
        var courierSessionHost = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CourierSession",
            "CourierSessionHostFactory.cs"));

        Assert.Contains("new FindCourierActorReq(courierId)", courierSessionBinder, StringComparison.Ordinal);
        Assert.Contains("new EnsureCourierActorReq(courierId)", courierSessionBinder, StringComparison.Ordinal);
        Assert.Contains("context.Actors.BindOrGetAsync", courierSessionBinder, StringComparison.Ordinal);
        Assert.Contains("builder.Services.AddSingleton<CourierSessionBinder>()", courierSessionHost,
            StringComparison.Ordinal);
        Assert.Contains("await binder.BindAsync(request.CourierId", courierSessionHandler, StringComparison.Ordinal);
        Assert.Contains("context.Client.Reply(bound).Submit()", courierSessionHandler, StringComparison.Ordinal);
    }

    [Fact]
    public void DeliveryDispatch_Dispatch_Uses_Readiness_Health_Without_Business_Request_Retry()
    {
        var sampleRoot = ResolveSampleRoot("DeliveryDispatch");
        var dispatchHost = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Dispatch",
            "DispatchServerHostFactory.cs"));
        var dispatchAdapters = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Dispatch",
            "DispatchZLinkAdapters.cs"));
        var dispatchWorker = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Dispatch",
            "DispatchWorker.cs"));

        Assert.Contains("IZLinkLocationReadiness", dispatchHost, StringComparison.Ordinal);
        Assert.Contains("readiness.IsPeerReadyAsync", dispatchHost, StringComparison.Ordinal);
        Assert.DoesNotContain("HasReadyCourierRouteAsync", dispatchHost, StringComparison.Ordinal);
        Assert.Contains("StatusCodes.Status503ServiceUnavailable", dispatchHost, StringComparison.Ordinal);
        Assert.DoesNotContain("new FindCourierActorReq(\"__health__\")", dispatchHost, StringComparison.Ordinal);
        Assert.DoesNotContain("RequestWithRetryAsync", dispatchAdapters, StringComparison.Ordinal);
        Assert.DoesNotContain("Task.Delay(100", dispatchAdapters, StringComparison.Ordinal);
        Assert.DoesNotContain("catch (Exception error)", dispatchAdapters, StringComparison.Ordinal);
        Assert.DoesNotContain("catch (Exception error)", dispatchWorker, StringComparison.Ordinal);
        Assert.DoesNotContain("courier timeout", dispatchWorker, StringComparison.Ordinal);
        Assert.Contains("RequestAsync<FindCourierActorReq, FindCourierActorRes>", dispatchAdapters,
            StringComparison.Ordinal);
        Assert.Contains("RequestAsync<OfferDeliveryReq, OfferDeliveryRes>", dispatchAdapters,
            StringComparison.Ordinal);
        Assert.Contains("RequestAsync<DeliveryStatusChangedReq, DeliveryStatusChangedRes>", dispatchAdapters,
            StringComparison.Ordinal);
    }
}
