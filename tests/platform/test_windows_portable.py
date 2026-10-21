"""Exercise the actual packaged launcher with a small GUI substitute; stdlib only."""
import hashlib
import ctypes
from contextlib import contextmanager
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


def start(exe, report, delay=0, code=37, args=(), cwd=None, env=None):
    p = subprocess.Popen([str(exe), str(report), str(delay), str(code), *args],
                         cwd=cwd, env=dict(clean_env, **(env or {})), stderr=subprocess.PIPE)
    processes.append(p)
    return p


def close_error(p, expected):
    _, stderr = p.communicate(timeout=20)
    assert p.returncode == 1, stderr
    assert expected in stderr.decode('utf-8'), stderr


def remove_runtime(path):
    # Resolve and validate every recursive deletion against this test's payload identities.
    path = path.resolve()
    assert path.parent == cache_base.resolve()
    assert any(path.name == cache.name or path.name.startswith(cache.name + '.tmp-') for cache in caches)
    if path.exists():
        shutil.rmtree(path)


@contextmanager
def deny_delete(path):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                  ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    # Allow reads/writes, but block deletion while simulating an external scanner's handle.
    handle = kernel.CreateFileW(str(path), 0x80000000, 3, None, 3, 0, None)
    assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
    try:
        yield
    finally:
        assert kernel.CloseHandle(handle)


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

        # Never modify a runtime being used by a running GUI; use a separate verified cache.
        live_report = work / 'live'
        live = start(exe, live_report, delay=4000)
        wait_report(live_report)
        (cache / 'plugins/nested/sample.bin').write_bytes(b'truncated')
        assert start(exe, work / 'replacement').wait(timeout=20) == 37
        fallback = Path(wait_report(work / 'replacement')[1]).parent
        assert fallback != cache and fallback.name.startswith(cache.name + '.tmp-')
        assert (cache / 'plugins/nested/sample.bin').read_bytes() == b'truncated'
        assert (fallback / 'plugins/nested/sample.bin').read_bytes() == payload.read_bytes()
        assert live.wait(timeout=10) == 37
        assert start(exe, work / 'fallback-reused').wait(timeout=20) == 37
        assert Path(wait_report(work / 'fallback-reused')[1]).parent == fallback
        remove_runtime(fallback)
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
            (abandoned / '.complete').write_text(cache.name)
            (cache / '.complete').unlink()
            assert start(exe, work / 'interrupted').wait(timeout=20) == 37
            assert (abandoned / 'incomplete').read_text() == 'partial extraction'
        finally:
            remove_runtime(abandoned)

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

        # Real Win32 handles can block cache deletion without an UpgradeTool lease.
        pinned = cache / 'plugins/nested/sample.bin'
        (cache / '.complete').unlink()
        with deny_delete(pinned):
            assert start(exe, work / 'locked-cache').wait(timeout=20) == 37
            fallback = Path(wait_report(work / 'locked-cache')[1]).parent
            assert fallback != cache
            assert pinned.read_bytes() == payload.read_bytes()
        remove_runtime(fallback)
        assert start(exe, work / 'after-locked-cache').wait(timeout=20) == 37

        # Compile the same launcher with a test-only Win32 rename shim for deterministic failures.
        fault_exe = work / 'fault-launcher.exe'
        run([compiler_bin / 'g++.exe', '-std=c++20', '-O2', '-Wall', '-Wextra', '-static',
             '-municode', '-mwindows', root / 'tests/platform/portable_launcher_faults.cpp',
             stage / '.single-file/payload.o', '-o', fault_exe,
             '-lcabinet', '-lbcrypt', '-lshell32', '-lole32', '-luuid'], env=compiler_env)
        rename_log = work / 'rename-attempts'
        fault_env = {'UPGRADE_TEST_RENAME_LOG': str(rename_log), 'UPGRADE_TEST_RENAME_FAILURES': '2'}
        (cache / '.complete').unlink()
        assert start(fault_exe, work / 'retry-success', env=fault_env).wait(timeout=20) == 37
        assert len(rename_log.read_text().splitlines()) == 3
        assert Path(wait_report(work / 'retry-success')[1]).parent == cache

        rename_log.unlink()
        fault_env['UPGRADE_TEST_RENAME_FAILURES'] = '99'
        (cache / '.complete').unlink()
        assert start(fault_exe, work / 'rename-blocked', env=fault_env).wait(timeout=20) == 37
        assert len(rename_log.read_text().splitlines()) == 5
        fallback = Path(wait_report(work / 'rename-blocked')[1]).parent
        assert fallback != cache and (fallback / '.complete').read_text() == cache.name
        fallback_snapshot = {str(p.relative_to(fallback)): (p.stat().st_mtime_ns, p.stat().st_size)
                             for p in fallback.rglob('*') if p.is_file()}
        reused = [start(exe, work / f'reuse-{i}', delay=100) for i in range(3)]
        assert [p.wait(timeout=20) for p in reused] == [37] * 3
        for i in range(3):
            assert Path(wait_report(work / f'reuse-{i}')[1]).parent == fallback
        assert fallback_snapshot == {str(p.relative_to(fallback)): (p.stat().st_mtime_ns, p.stat().st_size)
                                     for p in fallback.rglob('*') if p.is_file()}
        assert len(rename_log.read_text().splitlines()) == 5
        remove_runtime(fallback)
        assert start(exe, work / 'after-faults').wait(timeout=20) == 37

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
        print('PASS: concurrency, Unicode/quoting/CWD, exit codes, cache reuse, live/locked cache recovery, rename retries/fallback/reuse, repair, interrupted extraction, corrupt payload, launch failure and update isolation')
finally:
    for p in processes:
        if p.poll() is None:
            subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True)
            p.wait(timeout=10)
    for cache in caches:
        # Delete only the specific fixture payload identities created by this test.
        assert cache.parent == cache_base and len(cache.name) == 64
        remove_runtime(cache)
        for fallback in cache_base.glob(cache.name + '.tmp-*'):
            remove_runtime(fallback)
        cache.with_suffix('.lock').unlink(missing_ok=True)
