package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.IntStream;

public final class BingoCard {
    private static final int Size = 5;
    private static final int CellCount = Size * Size;
    private static final int FreeCellIndex = 12;

    private final List<Integer> numbers;
    private final List<Boolean> marks;

    private BingoCard(List<Integer> numbers, List<Boolean> marks) {
        this.numbers = numbers;
        this.marks = marks;
    }

    public static BingoCard create() {
        List<Integer> numbers = new ArrayList<>(
            IntStream.rangeClosed(1, CellCount).boxed().toList());
        numbers.set(FreeCellIndex, 0);
        List<Boolean> marks = new ArrayList<>();
        for (int i = 0; i < CellCount; i++) {
            marks.add(i == FreeCellIndex);
        }
        return new BingoCard(numbers, marks);
    }

    public void markDrawnNumber(int number) {
        for (int i = 0; i < numbers.size(); i++) {
            if (numbers.get(i) == number) {
                marks.set(i, true);
            }
        }
    }

    public int completedLines() {
        int completed = 0;
        for (int row = 0; row < Size; row++) {
            completed += isRowComplete(row) ? 1 : 0;
        }
        for (int column = 0; column < Size; column++) {
            completed += isColumnComplete(column) ? 1 : 0;
        }
        completed += isDiagonalComplete(0, Size + 1) ? 1 : 0;
        completed += isDiagonalComplete(Size - 1, Size - 1) ? 1 : 0;
        return completed;
    }

    public List<Integer> numbersSnapshot() {
        return List.copyOf(numbers);
    }

    public List<Boolean> marksSnapshot() {
        return List.copyOf(marks);
    }

    private boolean isRowComplete(int row) {
        int start = row * Size;
        for (int i = 0; i < Size; i++) {
            if (!marks.get(start + i)) {
                return false;
            }
        }
        return true;
    }

    private boolean isColumnComplete(int column) {
        for (int row = 0; row < Size; row++) {
            if (!marks.get((row * Size) + column)) {
                return false;
            }
        }
        return true;
    }

    private boolean isDiagonalComplete(int start, int step) {
        for (int i = 0; i < Size; i++) {
            if (!marks.get(start + (i * step))) {
                return false;
            }
        }
        return true;
    }
}
