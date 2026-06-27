using RegistrationCodec.Client;
using RegistrationCodec.Client.Scenarios;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

using var server = ZLinkHttpClient.Create(options.ServerUrl).Json().Build();
using var codecRequester = ZLinkHttpClient.Create(options.CodecRequesterUrl).Json().Build();

await AutoRegistrationScenario.RunAsync(server);
await AttributeRegistrationScenario.RunAsync(server);
await ManualRegistrationScenario.RunAsync(server);
await DiLifecycleAndFilterOrderingScenario.RunAsync(server);
await InvalidRegistrationScenario.RunAsync(options);
await CodecRoundTripScenario.RunAsync(server);
await CodecMismatchScenario.RunAsync(codecRequester);

Console.WriteLine("registration-codec e2e result=passed");
