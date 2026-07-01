import type { AssignDeliveryReq } from '../../Shared/Contracts/messages';

class DispatchWorkQueue {
  private readonly items: AssignDeliveryReq[] = [];
  private readonly waiters: Array<(item: AssignDeliveryReq) => void> = [];

  enqueue(request: AssignDeliveryReq): void {
    const waiter = this.waiters.shift();
    if (waiter !== undefined) {
      waiter(request);
      return;
    }
    this.items.push(request);
  }

  async next(): Promise<AssignDeliveryReq> {
    const item = this.items.shift();
    if (item !== undefined) {
      return item;
    }
    return await new Promise((resolve) => this.waiters.push(resolve));
  }
}

export { DispatchWorkQueue };
