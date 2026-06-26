using YieldDispatch.Client.Scenarios;
using YieldDispatch.Client.Support;

var options = ClientOptions.Parse(args);

switch (options.Scenario)
{
    case "full":
        await FullYieldDispatchScenario.RunAsync(options);
        break;
    case "shutdown-wait":
        await ShutdownYieldScenario.RunWaitAsync(options);
        break;
    case "shutdown-recovery":
        await ShutdownYieldScenario.RunRecoveryAsync(options);
        break;
    default:
        throw new ArgumentException($"Unknown scenario: {options.Scenario}");
}
