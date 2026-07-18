// Verifies TD-A1 Terminator Surface behavior.
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Workers;
using Zlink.HttpClient;

namespace AutomaticTurnDispatch.Client.Scenarios;
internal static class TdA1TerminatorSurfaceScenario
{
    public static Task RunAsync(ExecutionTurnScenarioContext context)
    {
        // 10.0.0 terminator sets (dotnet spec 02-handler-interfaces): request
        // and join calls complete with Async/Yield only; fire-and-forget Submit
        // remains a worker-call and HTTP-server terminator.
        AssertAwaitedTerminators(typeof(IZLinkRequestCall));
        AssertAwaitedTerminators(typeof(IZLinkActorJoinCall));
        AssertTerminators(typeof(IZLinkWorkerCall<>));
        AssertTerminators(typeof(ZLinkHttpServerRequestBuilder));

        var methods = typeof(ZLinkHttpRequestBuilder).GetMethods()
            .Select(method => method.Name)
            .ToHashSet(StringComparer.Ordinal);
        ZlinkStreamAssert.Ensure(methods.Contains("Async"),
            "TD-A1 standalone HTTP request builder is missing Async.");
        ZlinkStreamAssert.Ensure(!methods.Contains("Submit") && !methods.Contains("Yield"),
            "TD-A1 standalone HTTP request builder exposes server-only terminators.");
        ZlinkStreamAssert.Ensure(!methods.Contains("Fetch"),
            "TD-A1 standalone HTTP request builder exposes blocking Fetch.");
        return Task.CompletedTask;
    }

    private static void AssertAwaitedTerminators(Type type)
    {
        var methods = type.GetMethods().Select(method => method.Name).ToHashSet(StringComparer.Ordinal);
        foreach (var name in new[] { "Async", "Yield" })
            ZlinkStreamAssert.Ensure(methods.Contains(name), $"TD-A1 {type.Name} is missing {name}.");
        ZlinkStreamAssert.Ensure(!methods.Contains("Submit"),
            $"TD-A1 {type.Name} exposes the fire-and-forget Submit terminator.");
        ZlinkStreamAssert.Ensure(!methods.Contains("Fetch"), $"TD-A1 {type.Name} exposes blocking Fetch.");
    }

    private static void AssertTerminators(Type type)
    {
        var methods = type.GetMethods().Select(method => method.Name).ToHashSet(StringComparer.Ordinal);
        foreach (var name in new[] { "Submit", "Async", "Yield" })
            ZlinkStreamAssert.Ensure(methods.Contains(name), $"TD-A1 {type.Name} is missing {name}.");
        ZlinkStreamAssert.Ensure(!methods.Contains("Fetch"), $"TD-A1 {type.Name} exposes blocking Fetch.");
    }
}
