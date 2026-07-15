// Verifies TD-F6 Self Request Timeout Recovery behavior.
namespace AutomaticTurnDispatch.Client.Scenarios;
internal static class TdF6SelfRequestTimeoutRecoveryScenario { public static Task RunAsync(ExecutionTurnScenarioSuite suite) => suite.TdF6Async(); }
