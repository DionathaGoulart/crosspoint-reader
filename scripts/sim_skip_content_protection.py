"""Use the simulator's host binding instead of the device crypto binding.

Copied from crosspoint-simulator (MIT License, Copyright (c) 2026 Julia Nguyen)
so the simulator environments do not reach outside this repository for a build
script. It is three lines of
PlatformIO middleware; keeping a copy here means `pio run -e simulator_x4_classic`
fails with a missing *library* when the sibling clone is absent, which
PlatformIO reports clearly, instead of a missing extra_script, which it does not.
"""

Import("env")


def skip_device_binding(node):
    return None


env.AddBuildMiddleware(skip_device_binding, "*lib/Epub/ContentProtection.cpp")
