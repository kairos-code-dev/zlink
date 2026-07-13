import type { AssignDeliveryMsg } from '../../Shared/Contracts/messages';

class DispatchWorkQueue {
  private readonly items: AssignDeliveryMsg[] = [];
  private readonly waiters: Array<(item: AssignDeliveryMsg) => void> = [];

  enqueue(request: AssignDeliveryMsg): void {
    const waiter = this.waiters.shift();
    if (waiter !== undefined) {
      waiter(request);
      return;
    }
    this.items.push(request);
  }

  async next(): Promise<AssignDeliveryMsg> {
    const item = this.items.shift();
    if (item !== undefined) {
      return item;
    }
    return await new Promise((resolve) => this.waiters.push(resolve));
  }
}

export { DispatchWorkQueue };
