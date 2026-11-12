"""Pytest configuration."""

import pytest


# Use asyncio mode for all async tests
def pytest_collection_modifyitems(config, items):
    """Auto-mark all async tests."""
    for item in items:
        if item.get_closest_marker("asyncio") is None:
            if asyncio_test(item):
                item.add_marker(pytest.mark.asyncio)


def asyncio_test(item):
    """Check if test is async."""
    return hasattr(item, "function") and hasattr(item.function, "__wrapped__")
