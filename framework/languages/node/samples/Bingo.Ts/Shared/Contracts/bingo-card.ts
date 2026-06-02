class BingoCard {
  [key: string]: any;
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

export { BingoCard };
