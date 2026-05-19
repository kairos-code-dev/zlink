namespace Bingo.SessionGateway;

internal sealed class BingoCard
{
    public const int Size = 5;
    public const int CellCount = Size * Size;
    public const int FreeCellIndex = 12;

    private readonly int[] _numbers;
    private readonly bool[] _marks;

    private BingoCard(int[] numbers, bool[] marks)
    {
        _numbers = numbers;
        _marks = marks;
    }

    public static BingoCard CreateForSeat(int seat)
    {
        _ = seat;
        var numbers = Enumerable.Range(1, 75)
            .Take(CellCount)
            .ToArray();
        numbers[FreeCellIndex] = 0;

        var marks = new bool[CellCount];
        marks[FreeCellIndex] = true;
        return new BingoCard(numbers, marks);
    }

    public int CompletedLines => CountCompletedLines();

    public int[] NumbersSnapshot() => (int[])_numbers.Clone();

    public bool[] MarksSnapshot() => (bool[])_marks.Clone();

    public bool MarkDrawnNumber(int number)
    {
        var marked = false;
        for (var i = 0; i < _numbers.Length; i++)
        {
            if (_numbers[i] == number && !_marks[i])
            {
                _marks[i] = true;
                marked = true;
            }
        }

        return marked;
    }

    private int CountCompletedLines()
    {
        var completed = 0;
        for (var row = 0; row < Size; row++)
        {
            if (IsRowComplete(row))
            {
                completed++;
            }
        }

        for (var column = 0; column < Size; column++)
        {
            if (IsColumnComplete(column))
            {
                completed++;
            }
        }

        if (IsDiagonalComplete(0, Size + 1))
        {
            completed++;
        }

        if (IsDiagonalComplete(Size - 1, Size - 1))
        {
            completed++;
        }

        return completed;
    }

    private bool IsRowComplete(int row)
    {
        var start = row * Size;
        for (var i = 0; i < Size; i++)
        {
            if (!_marks[start + i])
            {
                return false;
            }
        }

        return true;
    }

    private bool IsColumnComplete(int column)
    {
        for (var row = 0; row < Size; row++)
        {
            if (!_marks[(row * Size) + column])
            {
                return false;
            }
        }

        return true;
    }

    private bool IsDiagonalComplete(int start, int step)
    {
        for (var i = 0; i < Size; i++)
        {
            if (!_marks[start + (i * step)])
            {
                return false;
            }
        }

        return true;
    }
}
