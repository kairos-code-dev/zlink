using RuntimeMonitoring.Client.Scenarios;
using RuntimeMonitoring.Client.Support;

var options = ClientOptions.Parse(args);

await MonA1SocketEventsScenario.RunAsync(options);
await MonA2RegistryEventsScenario.RunAsync(options);
await MonA3SpotEventsScenario.RunAsync(options);
await MonA4AvailabilityTransitionScenario.RunAsync(options);
await MonA5FixedKindsScenario.RunAsync(options);
await MonB1KindFilterScenario.RunAsync(options);
await MonB2RegistrationValidationScenario.RunAsync(options);
await MonC1DispatchFailureScenario.RunAsync(options);
await MonD1FailureRecoveryScenario.RunAsync(options);

Console.WriteLine("runtime-monitoring client result=passed");
