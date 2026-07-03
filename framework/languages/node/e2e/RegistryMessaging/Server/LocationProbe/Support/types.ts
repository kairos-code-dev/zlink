export interface HttpRoute {
  readonly method: string;
  readonly path: string;
  readonly handle: () => Promise<unknown> | unknown;
}
