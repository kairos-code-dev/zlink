using Bingo.SessionGateway;

var result = await BingoScenario.RunAsync();

Console.WriteLine("Bingo session gateway sample completed.");
Console.WriteLine($"room={result.FinalState.RoomId}");
Console.WriteLine($"status={result.FinalState.Status}");
Console.WriteLine($"host={result.FinalState.HostActorId}");
Console.WriteLine($"drawSeq={result.FinalState.DrawSeq}");
Console.WriteLine($"winners={string.Join(",", result.FinalState.Winners)}");
