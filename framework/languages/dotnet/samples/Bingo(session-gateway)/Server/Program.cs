using Bingo.SessionGateway.Server.Scenario;

var result = await BingoSessionGatewaySampleScenario.RunAsync();
result.WriteTo(Console.Out);
