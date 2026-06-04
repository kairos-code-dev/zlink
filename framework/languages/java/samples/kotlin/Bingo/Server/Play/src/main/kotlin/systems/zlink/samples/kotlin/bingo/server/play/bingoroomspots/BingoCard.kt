package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

class BingoCard private constructor(
    private val numbers: MutableList<Int>,
    private val marks: MutableList<Boolean>,
) {
    fun markDrawnNumber(number: Int) {
        numbers.forEachIndexed { index, value ->
            if (value == number) {
                marks[index] = true
            }
        }
    }

    fun completedLines(): Int {
        var completed = 0
        for (row in 0 until Size) {
            if (isRowComplete(row)) {
                completed++
            }
        }
        for (column in 0 until Size) {
            if (isColumnComplete(column)) {
                completed++
            }
        }
        if (isDiagonalComplete(0, Size + 1)) {
            completed++
        }
        if (isDiagonalComplete(Size - 1, Size - 1)) {
            completed++
        }
        return completed
    }

    fun numbersSnapshot(): List<Int> = numbers.toList()

    fun marksSnapshot(): List<Boolean> = marks.toList()

    private fun isRowComplete(row: Int): Boolean {
        val start = row * Size
        return (0 until Size).all { marks[start + it] }
    }

    private fun isColumnComplete(column: Int): Boolean =
        (0 until Size).all { row -> marks[(row * Size) + column] }

    private fun isDiagonalComplete(start: Int, step: Int): Boolean =
        (0 until Size).all { marks[start + (it * step)] }

    companion object {
        private const val Size = 5
        private const val CellCount = Size * Size
        private const val FreeCellIndex = 12

        fun create(): BingoCard {
            val numbers = (1..CellCount).toMutableList()
            numbers[FreeCellIndex] = 0
            val marks = MutableList(CellCount) { it == FreeCellIndex }
            return BingoCard(numbers, marks)
        }
    }
}
