"""Verify offline packaging failures without contacting the network."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cmake, root = sys.argv[1:]
script = Path(root) / 'scripts' / 'prepare_adb.cmake'
with tempfile.TemporaryDirectory(prefix='adb-packaging-') as temporary:
    work = Path(temporary)
    destination = work / 'stage with spaces' / 'adb'
    destination.mkdir(parents=True)
    marker = destination / 'existing-runtime'
    marker.write_text('preserve on verification failure')
    invalid = work / 'corrupt archive.zip'
    invalid.write_bytes(b'corrupt archive')
    base = [cmake, f'-DADB_CACHE_DIR={work / "cache"}',
            f'-DADB_DESTINATION={destination}']

    def reject(platform, archive, expected):
        environment = dict(os.environ, UPGRADE_TOOL_ADB_ARCHIVE=str(archive))
        result = subprocess.run([*base, f'-DADB_PLATFORM={platform}', '-P', str(script)],
                                env=environment, capture_output=True, text=True, timeout=10)
        assert result.returncode != 0, result.stdout
        assert expected in result.stdout + result.stderr, result.stderr
        assert marker.read_text() == 'preserve on verification failure'
        assert not list((work / 'cache').glob('*.download'))

    reject('win', invalid, 'SHA256 verification')
    reject('darwin', invalid, 'SHA256 verification')
    reject('win', work / 'missing.zip', 'ADB archive not found')
    reject('linux', invalid, 'Unsupported ADB platform')
