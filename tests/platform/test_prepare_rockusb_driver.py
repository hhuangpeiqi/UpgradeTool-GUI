"""Validate complete offline driver staging without executing any installer."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

cmake, root = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='rockusb-packaging-') as temporary:
    work = Path(temporary)
    scripts = work / 'scripts'
    scripts.mkdir()
    shutil.copy(Path(root) / 'scripts/prepare_rockusb_driver.cmake', scripts)
    source = work / 'original with spaces'
    source.mkdir()
    files = {'DriverInstall.exe': b'fixture executable', 'Driver/x64/rockusb.inf': b'fixture inf',
             'bin/x64/DIFxAPI.dll': b'fixture DLL', 'config.ini': b'fixture configuration'}
    manifest = 'set(ROCKUSB_DRIVER_FILES\n'
    for relative, content in files.items():
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        manifest += f'  "{relative}|{hashlib.sha256(content).hexdigest()}"\n'
    (scripts / 'rockusb_driver_manifest.cmake').write_text(manifest + ')\n')
    destination = work / 'stage with spaces'
    destination.mkdir()
    marker = destination / 'existing-runtime'
    marker.write_bytes(b'preserve')
    command = [cmake, f'-DROCKUSB_DRIVER_CACHE_DIR={work / "cache"}',
               f'-DROCKUSB_DRIVER_DESTINATION={destination}', '-P', str(scripts / 'prepare_rockusb_driver.cmake')]

    def run(expected=None):
        result = subprocess.run(command, env=dict(os.environ, UPGRADE_TOOL_ROCKUSB_DRIVER=str(source)),
                                capture_output=True, text=True, timeout=10)
        if expected:
            assert result.returncode != 0
            assert expected in result.stdout + result.stderr, result.stderr
        else:
            assert result.returncode == 0, result.stdout + result.stderr
        assert marker.read_bytes() == b'preserve'

    # Both missing dependencies and tampering fail before copying any payload.
    (source / 'config.ini').unlink()
    run('missing: config.ini')
    assert not (destination / 'DriverInstall.exe').exists()
    (source / 'config.ini').write_bytes(b'corrupt')
    run('SHA256 mismatch: config.ini')
    assert not (destination / 'DriverInstall.exe').exists()
    (source / 'config.ini').write_bytes(files['config.ini'])
    run()
    for relative, content in files.items():
        assert (destination / relative).read_bytes() == content
