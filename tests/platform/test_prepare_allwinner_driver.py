"""Validate complete offline driver staging without executing any installer."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

cmake, root = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='allwinner-packaging-') as temporary:
    work = Path(temporary)
    scripts = work / 'scripts'
    scripts.mkdir()
    shutil.copy(Path(root) / 'scripts/prepare_allwinner_driver.cmake', scripts)
    source = work / 'original with spaces'
    source.mkdir()
    files = {'InstallUSBDrv.exe': b'fixture executable', 'Driver/x64/allwinner.inf': b'fixture inf',
             'bin/x64/DIFxAPI.dll': b'fixture DLL', 'config.ini': b'fixture configuration'}
    manifest = 'set(ALLWINNER_DRIVER_FILES\n'
    for relative, content in files.items():
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        manifest += f'  "{relative}|{hashlib.sha256(content).hexdigest()}"\n'
    (scripts / 'allwinner_driver_manifest.cmake').write_text(manifest + ')\n')
    destination = work / 'stage with spaces'
    destination.mkdir()
    marker = destination / 'existing-runtime'
    marker.write_bytes(b'preserve')
    command = [cmake, f'-DALLWINNER_DRIVER_CACHE_DIR={work / "cache"}',
               f'-DALLWINNER_DRIVER_DESTINATION={destination}', '-P', str(scripts / 'prepare_allwinner_driver.cmake')]

    def run(expected=None):
        result = subprocess.run(command, env=dict(os.environ, UPGRADE_TOOL_ALLWINNER_DRIVER=str(source)),
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
    assert not (destination / 'InstallUSBDrv.exe').exists()
    (source / 'config.ini').write_bytes(b'corrupt')
    run('SHA256 mismatch: config.ini')
    assert not (destination / 'InstallUSBDrv.exe').exists()
    (source / 'config.ini').write_bytes(files['config.ini'])
    run()
    for relative, content in files.items():
        assert (destination / relative).read_bytes() == content

    # Exercise download, nested archive discovery and verified-cache reuse with
    # a local URL. This is deterministic and never executes fixture programs.
    archive = work / 'original.zip'
    with zipfile.ZipFile(archive, 'w') as z:
        for relative, content in files.items():
            z.writestr('Drivers/AW_Driver/' + relative, content)
    manifest += ')\n'
    manifest += f'set(ALLWINNER_DRIVER_URL "{archive.as_uri()}")\n'
    manifest += f'set(ALLWINNER_DRIVER_SHA256 "{hashlib.sha256(archive.read_bytes()).hexdigest()}")\n'
    (scripts / 'allwinner_driver_manifest.cmake').write_text(manifest)
    environment = dict(os.environ, UPGRADE_TOOL_ALLWINNER_DRIVER='')
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    cached = work / 'cache/PhoenixSuit.zip'
    assert cached.read_bytes() == archive.read_bytes()
    archive.unlink()
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    cached.write_bytes(b'modified archive')
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=10)
    assert result.returncode != 0 and 'Cached Allwinner driver failed SHA256' in result.stdout + result.stderr
