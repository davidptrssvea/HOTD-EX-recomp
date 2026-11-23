"""Progress and resource limits shared by the two local Windows builders."""
import ctypes
import os
from pathlib import Path
import subprocess
import time


def available_memory():
    if os.name != 'nt':
        return None
    class Memory(ctypes.Structure):
        _fields_ = [('length', ctypes.c_uint32), ('load', ctypes.c_uint32)] + [
            (name, ctypes.c_uint64) for name in (
                'total', 'available', 'page_total', 'page_available',
                'virtual_total', 'virtual_available', 'extended')]
    memory = Memory()
    memory.length = ctypes.sizeof(memory)
    return memory.available if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)) else None


def build_jobs(requested=0, memory=None, cpus=None):
    if requested < 0:
        raise ValueError('--jobs must be zero (automatic) or a positive number.')
    if requested:
        return min(requested, 16)
    memory = available_memory() if memory is None else memory
    cpus = (os.cpu_count() or 1) if cpus is None else cpus
    # Lifted units can be large. Leave 2 GiB for Windows and ~1.5 GiB per job.
    limit = max(1, (memory - 2 * 1024**3) // (1536 * 1024**2)) if memory is not None else 2
    return max(1, min(cpus, limit, 8))


def run(args, cwd=None, stdout=None):
    command = list(map(str, args))
    print('+ ' + ' '.join(command), flush=True)
    started = time.monotonic()
    with subprocess.Popen(command, cwd=cwd, stdout=stdout) as process:
        while True:
            try:
                code = process.wait(timeout=20)
                break
            except subprocess.TimeoutExpired:
                print(f'Still working: {Path(command[0]).name} ({int(time.monotonic()-started)} seconds). Please keep Setup open.', flush=True)
        if code:
            raise subprocess.CalledProcessError(code, command)
