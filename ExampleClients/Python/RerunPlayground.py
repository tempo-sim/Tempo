# Copyright Tempo Simulation, LLC. All Rights Reserved

"""Direct-run launcher for the RerunPlayground example client.

Run it from anywhere:  python /path/to/ExampleClients/Python/RerunPlayground.py --auto

This is a thin wrapper around the ``RerunPlayground`` package that lives next to
this file; it puts that package's directory on sys.path so the import works
regardless of the current working directory. You can equivalently run the
package form from this directory: ``python -m RerunPlayground``.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from RerunPlayground.__main__ import main

if __name__ == "__main__":
    main()
