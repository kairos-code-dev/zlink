import {
  captureZLinkSpotSerialTurn,
  isCurrentZLinkSpotSerialTurn,
  runZLinkSpotSerialTurn,
  ZLinkExecutionBarrier,
  type ZLinkExecutionBarrierClaim,
  ZLinkSpotSerialTurn
} from '../execution';

export class ZLinkSpotSerialExecutor {
  private tail: Promise<unknown> = Promise.resolve();
  private depth = 0;
  private turnSequence = 0;
  private executionBarrier: ZLinkExecutionBarrier | undefined;
  activeTurnId = 0;

  constructor(
    private readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics,
    private readonly kind: 'entry' | 'user' | 'instance' = 'user',
    private readonly yieldAllowed = true
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

  setExecutionBarrier(barrier: ZLinkExecutionBarrier): void {
    if (
      this.executionBarrier !== undefined
      && this.executionBarrier !== barrier
    ) {
      throw new Error('ZLink Spot serial executor already belongs to another execution barrier.');
    }
    if (this.turnSequence !== 0 && this.executionBarrier === undefined) {
      throw new Error('ZLink execution barrier must be attached before the first serial turn.');
    }
    this.executionBarrier = barrier;
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
    return this.enqueue(operation, false);
  }

  private enqueue<T>(
    operation: () => Promise<T> | T,
    resumeExistingClaim: boolean
  ): Promise<T> {
    const queuedAt = process.hrtime.bigint();
    this.metrics?.change('zlink.spot.queue.depth', 1, { kind: this.kind });
    const result = new Promise<T>((resolve, reject) => {
      void this.scheduleQueuedTurn(
        operation,
        queuedAt,
        resolve,
        reject,
        resumeExistingClaim
      );
    });
    return result;
  }

  private async scheduleQueuedTurn<T>(
    operation: () => Promise<T> | T,
    queuedAt: bigint,
    resolve: (value: T) => void,
    reject: (reason: unknown) => void,
    resumeExistingClaim: boolean
  ): Promise<void> {
    let barrierClaim: ZLinkExecutionBarrierClaim | undefined;
    try {
      if (!resumeExistingClaim) {
        barrierClaim = await this.executionBarrier?.enter();
      }
    } catch (error) {
      this.metrics?.change('zlink.spot.queue.depth', -1, { kind: this.kind });
      reject(error);
      return;
    }
    const gate = this.tail.then(
      () => this.startQueuedTurn(operation, queuedAt, resolve, reject, barrierClaim),
      () => this.startQueuedTurn(operation, queuedAt, resolve, reject, barrierClaim)
    );
    this.tail = gate.catch(() => undefined);
  }

  private startQueuedTurn<T>(
    operation: () => Promise<T> | T,
    queuedAt: bigint,
    resolve: (value: T) => void,
    reject: (reason: unknown) => void,
    barrierClaim: ZLinkExecutionBarrierClaim | undefined
  ): Promise<void> {
    this.metrics?.change('zlink.spot.queue.depth', -1, { kind: this.kind });
    this.metrics?.duration(
      'zlink.spot.queue.wait.duration',
      Number(process.hrtime.bigint() - queuedAt) / 1e9,
      { kind: this.kind }
    );
    return this.runTurn(operation, resolve, reject, barrierClaim);
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
    reject: (reason: unknown) => void,
    barrierClaim?: ZLinkExecutionBarrierClaim
  ): Promise<void> {
    const turn = new ZLinkSpotSerialTurn(
      (resumeTurn, resume) => this.postResume(resumeTurn, resume),
      this.yieldAllowed
    );
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
    owner.then(
      () => barrierClaim?.release(),
      () => barrierClaim?.release()
    );
    owner.then(resolve, reject);
    return Promise.race([
      owner.then(() => undefined, () => undefined),
      turn.suspended
    ]);
  }

  private postResume(turn: ZLinkSpotSerialTurn, resume: () => void): boolean {
    void this.enqueue(async () => {
      turn.resetSuspension();
      resume();
      await turn.resumeOwnerUntilNextYield();
    }, true);
    return true;
  }
}
