# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Producer/consumer pump shared by all sensor loggers.

The producer drains the gRPC stream at wire speed and pushes into a small
bounded queue, dropping the oldest pending item when full so the server never
backpressures. The consumer runs the (synchronous, numpy/rerun) handler on a
worker thread via ``asyncio.to_thread`` so heavy decode + logging never blocks
the event loop that the gRPC streams and the control-panel marshalling run on.

rerun's logging API is thread-safe, and each handler call sets the timeline and
logs together within one ``to_thread`` invocation, so time/log stay consistent.
"""

import asyncio
import traceback


async def pump(source, handle, queue_size: int = 2, label: str = "") -> None:
    """Drive `source` (an async iterator) through `handle` (a sync callable).

    Runs until the source is exhausted or the task is cancelled.
    """
    queue: asyncio.Queue = asyncio.Queue(maxsize=queue_size)

    async def consumer():
        while True:
            item = await queue.get()
            try:
                await asyncio.to_thread(handle, item)
            except Exception:  # one bad frame shouldn't kill the stream
                print(f"  [stream {label}] handler error:\n{traceback.format_exc()}")

    consumer_task = asyncio.create_task(consumer())
    try:
        async for item in source:
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            queue.put_nowait(item)
    finally:
        consumer_task.cancel()
