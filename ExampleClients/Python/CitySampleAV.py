# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Direct-run launcher for the CitySampleAV example client.

Run it from anywhere:  python /path/to/ExampleClients/Python/CitySampleAV.py [options]

Puts ExampleClients/Python on sys.path, so the CitySampleAV package and the shared tempo_rerun
building blocks import regardless of the current working directory. See CitySampleAV/README.md.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from CitySampleAV.__main__ import main

if __name__ == "__main__":
    main()
