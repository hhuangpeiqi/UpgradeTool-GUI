"""Exercise the actual packaged launcher with a small GUI substitute; stdlib only."""
import hashlib
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import time

root, build, compiler_bin = (Path(p).resolve() for p in sys.argv[1:4])
compiler_env = dict(os.environ, PATH=str(compiler_bin) + os.pathsep + os.environ['PATH'])
clean_env = dict(os.environ, PATH=os.path.join(os.environ['SystemRoot'], 'System32'))
cache_base = Path(os.environ['LOCALAPPDATA']) / 'upgrade_tool' / 'runtime'
caches = []
processes = []


def run(command, **kwargs):
    result = subprocess.run(list(map(str, command)), stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=120, **kwargs)
    if result.returncode:
        print(result.stdout.decode(errors='replace'), flush=True)
        result.check_returncode()


def check_icon(exe):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
    kernel.LoadLibraryExW.restype = ctypes.c_void_p
    kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    module = kernel.LoadLibraryExW(str(exe), None, 2)  # LOAD_LIBRARY_AS_DATAFILE
    assert module, ctypes.get_last_error()
    try:
        group = kernel.FindResourceW(module, 1, 14)  # RT_GROUP_ICON
        assert group, f'Missing application icon: {exe}'
        data = kernel.LockResource(kernel.LoadResource(module, group))
        assert data
        assert struct.unpack('<HHH', ctypes.string_at(data, 6)) == (0, 1, 7)
        sizes = set()
        for index in range(7):
            entry = ctypes.string_at(data + 6 + index * 14, 14)
            width, height, _, _, _, bits, length, resource_id = struct.unpack('<BBBBHHIH', entry)
            assert width == height and bits == 32 and length > 0
            assert kernel.FindResourceW(module, resource_id, 3), 'Missing RT_ICON image'
            sizes.add(width or 256)
        assert sizes == {16, 24, 32, 48, 64, 128, 256}
    finally:
        kernel.FreeLibrary(module)


def wait_report(path):
    deadline = time.monotonic() + 20
    while not path.exists() and time.monotonic() < deadline:
        time.sleep(.02)
    assert path.exists(), f'Child did not start: {path}'
    return path.read_bytes().decode('utf-8').split('\0')[:-1]


def start(exe, report, delay=0, code=37, args=(), cwd=None):
    p = subprocess.Popen([str(exe), str(report), str(delay), str(code), *args],
                         cwd=cwd, env=clean_env, stderr=subprocess.PIPE)
    processes.append(p)
    return p


def close_error(p, expected):
    _, stderr = p.communicate(timeout=20)
    assert p.returncode == 1, stderr
    assert expected in stderr.decode('utf-8'), stderr


build.mkdir(parents=True, exist_ok=True)
try:
    with tempfile.TemporaryDirectory(prefix='portable-tests-', dir=build, ignore_cleanup_errors=True) as temporary:
        work = Path(temporary)
        stage = work / 'stage'
        stage.mkdir()
        run([compiler_bin / 'g++.exe', '-std=c++20', '-O2', '-static', '-municode',
             root / 'tests/platform/portable_fixture.cpp', '-o', stage / 'upgrade_tool_gui.exe'], env=compiler_env)
        nested = stage / 'plugins' / 'nested'
        nested.mkdir(parents=True)
        payload = nested / 'sample.bin'
        payload.write_bytes(os.urandom(1024 * 1024))

        def package(destination):
            run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                 root / 'scripts/windows/package_portable.ps1', '-Stage', stage,
                 '-Output', destination, '-CompilerBin', compiler_bin], env=compiler_env)
            identity = (stage / '.single-file/identity.txt').read_text()
            cache = cache_base / identity
            caches.append(cache)
            return cache

        exe = work / 'portable.exe'
        cache = package(exe)
        check_icon(exe)
        # Concurrent first launches share one complete runtime and preserve child exit codes.
        begin = time.perf_counter()
        first = [start(exe, work / f'first-{i}', delay=100) for i in range(3)]
        assert [p.wait(timeout=30) for p in first] == [37] * 3
        first_time = time.perf_counter() - begin
        assert (cache / '.complete').read_text() == cache.name
        assert hashlib.sha256((cache / 'plugins/nested/sample.bin').read_bytes()).digest() == hashlib.sha256(payload.read_bytes()).digest()
        snapshot = {str(p.relative_to(cache)): (p.stat().st_mtime_ns, p.stat().st_size)
                    for p in cache.rglob('*') if p.is_file()}

        unicode_dir = work / '\u4e2d\u6587 path'
        unicode_dir.mkdir()
        renamed = unicode_dir / '\u4fbf\u643a\u7248.exe'
        shutil.copyfile(exe, renamed)
        args = ['', '--firmware', '\u56fa\u4ef6 folder/relative.img', 'say "hello"', 'tail\\', 'two\\\\"quotes']
        report = work / 'warm-report'
        begin = time.perf_counter()
        assert start(renamed, report, args=args, cwd=unicode_dir).wait(timeout=15) == 37
        warm_time = time.perf_counter() - begin
        actual = wait_report(report)
        assert Path(actual[0]) == unicode_dir
        assert Path(actual[1]) == cache / 'upgrade_tool_gui.exe'
        assert actual[2:] == args, actual
        assert snapshot == {str(p.relative_to(cache)): (p.stat().st_mtime_ns, p.stat().st_size)
                            for p in cache.rglob('*') if p.is_file()}, 'Cache was rewritten on warm launch'
        print(f'Concurrent cold launch: {first_time:.3f}s; warm launch: {warm_time:.3f}s; cache unchanged')

        # Never repair a runtime being used by a running GUI.
        live_report = work / 'live'
        live = start(exe, live_report, delay=4000)
        wait_report(live_report)
        (cache / 'plugins/nested/sample.bin').write_bytes(b'truncated')
        close_error(start(exe, work / 'blocked'), 'in use')
        assert live.wait(timeout=10) == 37
        assert start(exe, work / 'repaired').wait(timeout=20) == 37
        assert (cache / 'plugins/nested/sample.bin').read_bytes() == payload.read_bytes()
        # A missing completion marker forces a verified rebuild.
        (cache / '.complete').unlink()
        assert start(exe, work / 'marker-repaired').wait(timeout=20) == 37

        # A same-sized broken executable passes the cheap cache check, but reports launch failure.
        cached_exe = cache / 'upgrade_tool_gui.exe'
        original = cached_exe.read_bytes()
        cached_exe.write_bytes(b'XX' + original[2:])
        close_error(start(exe, work / 'start-failed'), 'Cannot start UpgradeTool GUI')
        cached_exe.write_bytes(original)

        # An interrupted staging directory is never used as a complete runtime.
        abandoned = cache_base / (cache.name + '.tmp-abandoned-test')
        abandoned.mkdir()
        try:
            (abandoned / 'incomplete').write_text('partial extraction')
            (cache / '.complete').unlink()
            assert start(exe, work / 'interrupted').wait(timeout=20) == 37
            assert (abandoned / 'incomplete').read_text() == 'partial extraction'
        finally:
            shutil.rmtree(abandoned)

        # Reject a corrupt embedded cabinet before deleting an existing cache.
        cab_bytes = (stage / '.single-file/payload.cab').read_bytes()
        exe_bytes = bytearray(exe.read_bytes())
        offset = exe_bytes.find(cab_bytes)
        assert offset >= 0
        exe_bytes[offset + 128] ^= 1
        broken = work / 'corrupt.exe'
        broken.write_bytes(exe_bytes)
        (cache / '.complete').unlink()
        close_error(start(broken, work / 'corrupt'), 'Embedded package failed SHA256')
        assert cached_exe.read_bytes() == original
        assert start(exe, work / 'after-corrupt').wait(timeout=20) == 37

        # A new payload gets a separate cache while the previous runtime is alive.
        old_live = start(exe, work / 'old-live', delay=6000)
        wait_report(work / 'old-live')
        payload.write_bytes(b'updated payload')
        updated = work / 'updated.exe'
        new_cache = package(updated)
        assert new_cache != cache
        assert start(updated, work / 'updated-report').wait(timeout=20) == 37
        assert cache.exists() and new_cache.exists()
        assert old_live.wait(timeout=20) == 37
        print('PASS: concurrency, Unicode/quoting/CWD, exit codes, cache reuse, repair, interrupted extraction, corrupt payload, launch failure and update isolation')
finally:
    for p in processes:
        if p.poll() is None:
            subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True)
            p.wait(timeout=10)
    for cache in caches:
        # Delete only the specific fixture payload identities created by this test.
        assert cache.parent == cache_base and len(cache.name) == 64
        if cache.exists():
            shutil.rmtree(cache)
        cache.with_suffix('.lock').unlink(missing_ok=True)
