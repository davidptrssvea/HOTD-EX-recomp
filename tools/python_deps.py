"""Bootstrap the pure-Python ELF reader inside HOTD4's isolated build tools."""
import argparse
import hashlib
import importlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import sys
import time
import urllib.request
import zipfile

VERSION = '0.33'
SHA256 = 'f215ad5f47d3f1373a21496a6c9e0707c622840d0622f23ff7ce08678b020036'
WHEEL = 'pyelftools-0.33-py3-none-any.whl'
URL = 'https://files.pythonhosted.org/packages/46/2a/f9697576603dae937727827505a6126a066affb227034e77e6f9068910da/' + WHEEL


def digest(data):
    return hashlib.sha256(data).hexdigest()


def existing_reader():
    try:
        import elftools
        from elftools.elf.elffile import ELFFile
        return elftools.__version__ == VERSION and callable(ELFFile)
    except (ImportError, AttributeError, SyntaxError):
        return False


def ensure(cache, no_download=False):
    cache = Path(cache).resolve()
    target = cache / ('pyelftools-' + VERSION)
    archive = cache / WHEEL
    # Existing older builder caches remain usable without a download.
    if not target.exists() and existing_reader():
        print('Using existing pyelftools ' + VERSION + '.', flush=True)
        return
    data = archive.read_bytes() if archive.is_file() else b''
    if digest(data) != SHA256:
        if no_download:
            raise RuntimeError('Verified pyelftools cache is missing or damaged. Run Setup once without -NoDownload to repair it.')
        print('Downloading the pinned pyelftools dependency from PyPI...', flush=True)
        for attempt in range(3):
            try:
                url = URL if attempt < 2 else URL + '?hotd4_retry=' + str(time.time_ns())
                with urllib.request.urlopen(url, timeout=60) as response:
                    data = response.read(8 * 1024 * 1024)
                if digest(data) != SHA256:
                    raise RuntimeError('pyelftools download failed SHA-256 verification (received ' + digest(data) + ').')
                break
            except (OSError, RuntimeError) as error:
                print(f'Python dependency attempt {attempt+1}/3 failed: {error}', flush=True)
                if attempt == 2:
                    raise RuntimeError('pyelftools download failed after 3 attempts; no Python modules were installed. ' + str(error)) from error
                time.sleep(2 * (attempt + 1))
        cache.mkdir(parents=True, exist_ok=True)
        pending = cache / (WHEEL + '.partial')
        pending.write_bytes(data)
        os.replace(pending, archive)
    with zipfile.ZipFile(io.BytesIO(data)) as wheel:
        members = []
        for entry in wheel.infolist():
            path = PurePosixPath(entry.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in entry.filename or ':' in entry.filename:
                raise RuntimeError('Unsafe path in pyelftools wheel.')
            if entry.is_dir():
                continue
            if path.parts and path.parts[0] == 'elftools' and path.suffix == '.py':
                members.append((path, wheel.read(entry)))
            elif path.parts[:2] == ('pyelftools-' + VERSION + '.dist-info', 'licenses'):
                members.append((PurePosixPath('licenses', *path.parts[2:]), wheel.read(entry)))
        if not any(str(path) == 'elftools/elf/elffile.py' for path, _ in members):
            raise RuntimeError('pyelftools wheel is missing its ELF reader.')
        for relative, body in members:
            destination = target.joinpath(*relative.parts)
            # All destinations are under this version's owned module folder.
            if target not in destination.resolve().parents:
                raise RuntimeError('Invalid pyelftools destination.')
            destination.parent.mkdir(parents=True, exist_ok=True)
            pending = destination.with_suffix('.py.pending')
            pending.write_bytes(body)
            os.replace(pending, destination)
    (target / 'source.json').write_text(json.dumps({'version': VERSION, 'url': URL, 'sha256': SHA256}, indent=2) + '\n', encoding='utf-8')
    sys.path.insert(0, str(target))
    # Ensure validation uses this verified module tree, even in a reused process.
    for name in list(sys.modules):
        if name == 'elftools' or name.startswith('elftools.'):
            del sys.modules[name]
    importlib.invalidate_caches()
    if not existing_reader():
        raise RuntimeError('The verified pyelftools dependency could not be imported.')
    print('Verified pyelftools ' + VERSION + ' in the isolated build-tool cache.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', required=True)
    parser.add_argument('--no-download', action='store_true')
    args = parser.parse_args()
    ensure(args.cache, args.no_download)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('Python dependency setup failed: ' + str(error), file=sys.stderr)
        sys.exit(1)
