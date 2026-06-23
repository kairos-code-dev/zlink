export class ZLinkEncodedPayload {
  private readonly payload: Buffer;

  private constructor(bytes: Uint8Array) {
    this.payload = Buffer.from(bytes);
  }

  static from(bytes: Uint8Array): ZLinkEncodedPayload {
    return new ZLinkEncodedPayload(bytes);
  }

  data(): Uint8Array {
    return new Uint8Array(this.payload);
  }
}
