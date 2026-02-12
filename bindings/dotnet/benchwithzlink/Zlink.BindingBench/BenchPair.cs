using Zlink;

internal static partial class BenchRunner
{
    internal static int RunPair(string transport, int size)
      => RunPairLike("PAIR", SocketType.Pair, SocketType.Pair, transport, size);

    internal static int RunDealerDealer(string transport, int size)
      => RunPairLike("DEALER_DEALER", SocketType.Dealer, SocketType.Dealer, transport, size);
}
