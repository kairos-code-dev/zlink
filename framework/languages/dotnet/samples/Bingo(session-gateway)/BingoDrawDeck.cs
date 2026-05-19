namespace Bingo.SessionGateway;

internal sealed class BingoDrawDeck
{
    private readonly Queue<int> _numbers = new(Enumerable.Range(1, 75));

    public bool HasNext => _numbers.Count > 0;

    public int Draw()
    {
        if (_numbers.Count == 0)
        {
            throw new InvalidOperationException("Bingo draw deck is empty.");
        }

        return _numbers.Dequeue();
    }
}
