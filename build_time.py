# Inject the UTC build timestamp into the firmware.  The dashboard uses it as
# an offline fallback when the RX8130CE reports a lost or invalid clock value.
import time

Import("env")  # noqa: F821 - PlatformIO supplies env at build time.

env.Append(CPPDEFINES=[("BUILD_UNIX_TIME", int(time.time()))])
