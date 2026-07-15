// Verifies TD-D3 Timer No Reentry behavior.
namespace AutomaticTurnDispatch.Client.Scenarios;
internal static class TdD3TimerNoReentryScenario { public static Task RunAsync(ExecutionTurnScenarioSuite suite) => suite.TdD3Async(); }
