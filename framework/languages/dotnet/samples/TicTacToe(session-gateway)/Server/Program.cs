using TicTacToe.SessionActorDispatch;
using TicTacToe.SessionGateway.Server.Scenario;

var result = await SessionActorDispatchSampleScenario.RunAsync();

result.WriteTo(Console.Out);
