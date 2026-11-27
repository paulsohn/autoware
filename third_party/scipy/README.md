# SciPy provider

This module exposes SciPy 1.17.1 to numerical Python consumers. Root selects its
local override; consumers depend on @scipy. The archive pins the CPython 3.14
manylinux 2.27/2.28 x86_64 wheel by URL and SHA-256. Its bundled native extensions
and data remain together, and NumPy comes from the selected RCR Python hub.
This is the current Linux x86_64/Python 3.14 profile; other Python/platform
profiles need their own qualified inputs.

Run @scipy//... for the signal-filter/native linear-algebra check. When changing
this shared provider, also run @acados//... to validate its template generation,
native original suite and generated solver. Pinning Python packages here does
not install them into the builder host.
