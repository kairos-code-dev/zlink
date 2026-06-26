using RegistrationCodec.Server;
using RegistrationCodec.Server.Endpoints;

await RegistrationCodecServerHostFactory.Create(
        args,
        (app, options) => app.MapRegistrationScenarioEndpoints(options))
    .RunAsync();
