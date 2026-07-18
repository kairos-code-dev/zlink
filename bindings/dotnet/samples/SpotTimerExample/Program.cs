// 자립형 가이드 예제: SPOT timer.
// 게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).
//   dotnet run --project samples/SpotTimerExample
using SampleCommon;
using Systems.Zlink;

internal static class Program
{
    private static void Main(string[] args)
    {
        // --8<-- [start:doc]
        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateMeshNode();
        SampleSupport.StartConfiguredNode(node, "spot-timer-node");
        using var room = node.CreateSpot();
        // 게임룸의 이벤트 루프에서 디스패치되는 타이머를 만든다.
        using var timer = Zlink.CreateTimer(room);

        int ticks = 0;
        timer.OnFire((t, fireCount) => Interlocked.Increment(ref ticks));
        // 50ms 간격으로 3번 발화한다.
        timer.Start(TimeSpan.FromMilliseconds(50), 3);

        DateTime deadline = DateTime.UtcNow.AddSeconds(3);
        while (Volatile.Read(ref ticks) < 3 && DateTime.UtcNow < deadline)
        {
            Thread.Sleep(10);
        }
        int final = Volatile.Read(ref ticks);
        if (final < 3)
        {
            throw new Exception($"timer fired only {final} times");
        }
        Console.WriteLine($"[spot/timer] room tick fired {final} times");
        // --8<-- [end:doc]
    }
}
