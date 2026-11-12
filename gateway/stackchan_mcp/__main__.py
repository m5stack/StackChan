"""Entry point: ``python -m stackchan_mcp``.

The actual implementation lives in :mod:`stackchan_mcp.cli` so that the
console script and ``python -m`` paths share a single side-effect-free
import surface.
"""

from .cli import main


if __name__ == "__main__":
    main()
