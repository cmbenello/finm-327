# Momentum Strategy Explanation

The client uses a simple momentum detection approach based on the last 3 prices received from the server. Each time a new price comes in, it gets added to a `deque` that holds at most 3 values. Once we have 3 prices, we check if they form a strictly increasing or strictly decreasing sequence. If all three prices move in the same direction, that counts as momentum and we fire off an order for that price ID.

The idea is that if the price has been consistently moving up (or down) over the last 3 ticks, the trend is likely to continue for at least one more tick, so it makes sense to act on it. If the prices are choppy or flat, we skip. There's also a small random delay (10-50ms) before sending the order to simulate realistic network/reaction latency. The client tracks how many prices it received vs how many orders it actually sent, so you can see the hit rate at the end.
