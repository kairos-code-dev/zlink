using RegistrationCodec.Client.Scenarios;
using Zlink.HttpClient;
using RegistrationCodec.Client.Support;

var options = ClientOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);

using var server = ZLinkHttpClient.Create(options.ServerUrl).Json().Build();
using var codecRequester = ZLinkHttpClient.Create(options.CodecRequesterUrl).Json().Build();

await AutoRegistrationScenario.RunAsync(server);
await AttributeRegistrationScenario.RunAsync(server);
await ManualRegistrationScenario.RunAsync(server);
await RcA4DiLifecycleScenario.RunAsync(server);
await RcA5FilterOrderingScenario.RunAsync(server);
await InvalidRegistrationScenario.RunAsync(options);
await RcB1JsonCodecScenario.RunAsync(server);
await RcB2ProtobufCodecScenario.RunAsync(server);
await RcB3MessagePackCodecScenario.RunAsync(server);
await RcB4CodecCoexistenceScenario.RunAsync(server);
await CodecMismatchScenario.RunAsync(codecRequester);

Console.WriteLine("registration-codec e2e result=passed");
