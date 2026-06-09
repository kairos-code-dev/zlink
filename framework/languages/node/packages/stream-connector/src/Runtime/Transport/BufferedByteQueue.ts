export class BufferedByteQueue {
  private readonly chunks: Buffer[] = [];
  private bufferedBytes = 0;

  get size(): number {
    return this.bufferedBytes;
  }

  push(chunk: Buffer): void {
    this.chunks.push(chunk);
    this.bufferedBytes += chunk.length;
  }

  peek(length: number): Uint8Array {
    const output = new Uint8Array(length);
    let offset = 0;
    for (const chunk of this.chunks) {
      const take = Math.min(chunk.length, length - offset);
      output.set(chunk.subarray(0, take), offset);
      offset += take;
      if (offset === length) {
        break;
      }
    }
    return output;
  }

  consume(length: number): Uint8Array {
    const output = new Uint8Array(length);
    let offset = 0;
    while (offset < length) {
      const chunk = this.chunks[0];
      const take = Math.min(chunk.length, length - offset);
      output.set(chunk.subarray(0, take), offset);
      offset += take;
      this.bufferedBytes -= take;
      if (take === chunk.length) {
        this.chunks.shift();
      } else {
        this.chunks[0] = chunk.subarray(take);
      }
    }
    return output;
  }
}
