using TicTacToe.SessionActorDispatch;

var result = await SessionActorDispatchSampleScenario.RunAsync();

result.WriteTo(Console.Out);
