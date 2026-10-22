"""Verify pinned driver staging and offline failures without executing drivers."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

cmake, root = sys.argv[1:]
source_scripts = Path(root) / 'scripts'
with tempfile.TemporaryDirectory(prefix='qualcomm-packaging-') as temporary:
    work = Path(temporary)
    scripts = work / 'scripts'
    scripts.mkdir()
    shutil.copy(source_scripts / 'prepare_qualcomm_driver.cmake', scripts)
    shutil.copy(source_scripts / 'qualcomm_driver_manifest.cmake', scripts)
    (scripts / 'windows').mkdir()
    shutil.copy(source_scripts / 'windows' / 'qualcomm-LICENSE.txt', scripts / 'windows')
    destination = work / 'stage with spaces' / 'qualcomm'
    destination.mkdir(parents=True)
    marker = destination / 'existing-runtime'
    marker.write_text('preserve on verification failure')
    invalid = work / 'invalid driver.exe'
    invalid.write_bytes(b'not the official driver')
    base = [cmake, f'-DQUALCOMM_DRIVER_CACHE_DIR={work / "cache"}',
            f'-DQUALCOMM_DRIVER_DESTINATION={destination}']

    def run(source, expected):
        environment = dict(os.environ, UPGRADE_TOOL_QUALCOMM_DRIVER=str(source))
        result = subprocess.run([*base, '-P', str(scripts / 'prepare_qualcomm_driver.cmake')],
                                env=environment, capture_output=True, text=True, timeout=10)
        assert result.returncode != 0, result.stdout
        assert expected in result.stdout + result.stderr, result.stderr
        assert marker.read_text() == 'preserve on verification failure'

    run(invalid, 'SHA256 verification')
    run(work / 'missing.exe', 'not found')
    # Substitute only the fixture manifest to exercise successful offline staging.
    checksum = hashlib.sha256(invalid.read_bytes()).hexdigest()
    manifest = scripts / 'qualcomm_driver_manifest.cmake'
    original = manifest.read_text()
    import re
    manifest.write_text(re.sub(r'(set\(QUALCOMM_DRIVER_SHA256 ")[a-f0-9]+',
                               lambda m: m[1] + checksum, original))
    result = subprocess.run([*base, f'-DQUALCOMM_DRIVER_SOURCE={invalid}', '-P',
                             str(scripts / 'prepare_qualcomm_driver.cmake')],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert (destination / 'qcom_usb_kernel_drivers_x64.exe').read_bytes() == invalid.read_bytes()
    assert (destination / 'LICENSE.txt').read_bytes() == (scripts / 'windows' / 'qualcomm-LICENSE.txt').read_bytes()
