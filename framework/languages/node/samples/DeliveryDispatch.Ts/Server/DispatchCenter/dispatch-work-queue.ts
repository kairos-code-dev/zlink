import type { AssignDelivery } from '../../Shared/Contracts/messages';

class DispatchWorkQueue {
  private readonly items: AssignDelivery[] = [];
  private readonly waiters: Array<(item: AssignDelivery) => void> = [];

  enqueue(request: AssignDelivery): void {
    const waiter = this.waiters.shift();
    if (waiter !== undefined) {
      waiter(request);
      return;
    }
    this.items.push(request);
  }

  async next(): Promise<AssignDelivery> {
    const item = this.items.shift();
    if (item !== undefined) {
      return item;
    }
    return await new Promise((resolve) => this.waiters.push(resolve));
  }
}

export { DispatchWorkQueue };
