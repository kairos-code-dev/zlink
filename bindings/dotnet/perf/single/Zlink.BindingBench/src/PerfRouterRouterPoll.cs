using static PerfRunner;

internal static class PerfRouterRouterPoll
{
    internal static int RunRouterRouterPoll(string transport, int size)
      => PerfRouterRouter.RunRouterRouterInternal(transport, size, true);
}
