import {
  captureZLinkSpotSerialTurn,
  isCurrentZLinkSpotSerialTurn,
  runZLinkSpotSerialTurn,
  ZLinkSpotSerialTurn
} from '../execution';

export class ZLinkSpotSerialExecutor {
  private tail: Promise<unknown> = Promise.resolve();
  private depth = 0;
  private turnSequence = 0;
  activeTurnId = 0;

  constructor(
    private readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics,
    private readonly kind: 'entry' | 'user' | 'instance' = 'user'
  ) {}

  get isExecuting(): boolean {
    return this.depth > 0;
  }

  get isCurrentTurn(): boolean {
    return this.depth > 0 && isCurrentZLinkSpotSerialTurn(this);
  }

  get currentTurn(): ZLinkSpotSerialTurn | undefined {
    return captureZLinkSpotSerialTurn(this);
  }

  /**
   * Runs `operation` in serial order, one turn at a time. A call made from
   * within the currently active turn of this executor is re-entrant and runs
   * as part of that turn instead of queueing (which would deadlock a turn
   * that awaits the nested result).
   */
  execute<T>(operation: () => Promise<T> | T): Promise<T> {
    if (this.isCurrentTurn) {
      return Promise.resolve().then(operation);
    }
    return this.post(operation);
  }

  /**
   * Always enqueues `operation` as its own serial turn, even when called
   * from within the currently active turn. Detached completion callbacks use
   * this so they never run inline inside another callback's turn.
   */
  post<T>(operation: () => Promise<T> | T): Promise<T> {
    const queuedAt = process.hrtime.bigint();
    this.metrics?.change('zlink.spot.queue.depth', 1, { kind: this.kind });
    const result = new Promise<T>((resolve, reject) => {
      const gate = this.tail.then(
        () => this.startQueuedTurn(operation, queuedAt, resolve, reject),
        () => this.startQueuedTurn(operation, queuedAt, resolve, reject)
      );
      this.tail = gate.catch(() => undefined);
    });
    return result;
  }

  private startQueuedTurn<T>(
    operation: () => Promise<T> | T,
    queuedAt: bigint,
    resolve: (value: T) => void,
    reject: (reason: unknown) => void
  ): Promise<void> {
    this.metrics?.change('zlink.spot.queue.depth', -1, { kind: this.kind });
    this.metrics?.duration(
      'zlink.spot.queue.wait.duration',
      Number(process.hrtime.bigint() - queuedAt) / 1e9,
      { kind: this.kind }
    );
    return this.runTurn(operation, resolve, reject);
  }

  yieldPromise<T>(pending: Promise<T>): Promise<T> {
    const turn = this.currentTurn;
    if (turn === undefined) {
      throw new Error('yield requires a framework Spot handler turn.');
    }
    return turn.yieldPromise(pending);
  }

  private runTurn<T>(
    operation: () => Promise<T> | T,
    resolve: (value: T) => void,
    reject: (reason: unknown) => void
  ): Promise<void> {
    const turn = new ZLinkSpotSerialTurn((resumeTurn, resume) => this.postResume(resumeTurn, resume));
    const wrapped = async () => {
      this.depth += 1;
      this.turnSequence += 1;
      const turnId = this.turnSequence;
      this.activeTurnId = turnId;
      try {
        return await runZLinkSpotSerialTurn(this, turnId, turn, operation);
      } finally {
        this.depth -= 1;
        this.activeTurnId = 0;
      }
    };
    const owner = wrapped();
    turn.bindOwner(owner);
    owner.then(resolve, reject);
    return Promise.race([
      owner.then(() => undefined, () => undefined),
      turn.suspended
    ]);
  }

  private postResume(turn: ZLinkSpotSerialTurn, resume: () => void): boolean {
    void this.post(async () => {
      turn.resetSuspension();
      resume();
      await turn.resumeOwnerUntilNextYield();
    });
    return true;
  }
}
