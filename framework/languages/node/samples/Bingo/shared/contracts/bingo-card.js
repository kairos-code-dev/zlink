class BingoCard {
  constructor(numbers) {
    this.numbers = numbers;
    this.marked = new Set();
  }

  mark(number) {
    if (this.numbers.includes(number)) {
      this.marked.add(number);
    }
    return this.isComplete();
  }

  isComplete() {
    return this.numbers.every((number) => this.marked.has(number));
  }
}

module.exports = { BingoCard };
