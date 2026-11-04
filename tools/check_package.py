"""Validate local Markdown links, required source files and release checksums."""
from pathlib import Path
import hashlib
import re
from urllib.parse import unquote
root = Path(__file__).resolve().parents[1]
errors = []
for doc in root.rglob('*.md'):
    for match in re.finditer(r'!?\[[^\]]*\]\(([^\s)]+)\)', doc.read_text()):
        link = match.group(1).strip('<>')
        if '://' in link or link.startswith(('#', 'mailto:')):
            continue
        target = (doc.parent / unquote(link.split('#', 1)[0])).resolve()
        if not target.exists():
            errors.append(f'{doc.relative_to(root)}: broken link {link}')
for name in ('Soundpod_ESP32.ino', 'BambooGame.h', 'BatteryGauge.h', 'ButtonInput.h', 'PandaWalker.h'):
    if not (root / 'firmware/Soundpod_ESP32' / name).is_file():
        errors.append(f'Missing required sketch file: {name}')
manifest = root / 'FIRMWARE_SHA256SUMS.txt'
if manifest.exists():
    for line in manifest.read_text().splitlines():
        expected, filename = line.split('  ', 1)
        actual = hashlib.sha256((root / filename).read_bytes()).hexdigest()
        if actual != expected:
            errors.append(f'Checksum differs: {filename} (update the manifest after intentional changes)')
if errors:
    raise SystemExit('\n'.join(errors))
print('PASS: Markdown links, required files and firmware checksums.')
