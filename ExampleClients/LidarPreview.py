# Copyright Tempo Simulation, LLC. All Rights Reserved

import argparse
import asyncio

import tempo.TempoLidarUtils as tlu

async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', required=True)
    parser.add_argument('--owner', default="", required=False)
    parser.add_argument('--colorize_by', default="distance", required=False, choices=['distance', 'intensity', 'label'])
    parser.add_argument('--update_rate', default=30.0, required=False)
    args = parser.parse_args()

    await tlu.stream_lidar_scans(args.name, args.owner, args.colorize_by, args.update_rate)

if __name__ == "__main__":
    asyncio.run(main())
