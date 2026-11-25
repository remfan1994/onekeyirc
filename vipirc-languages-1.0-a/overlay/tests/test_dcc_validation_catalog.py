"""Audit stable DCC validation errors and system-error templates."""
from pathlib import Path
import json,re
root=Path(__file__).resolve().parent.parent
keys=set(json.loads((root/'i18n/en.json').read_text()))
errors=set(re.findall(r'problem\(error, cap, "([^"]+)"', (root/'src/dcc.c').read_text()))
assert errors
for error in errors:
    assert error.replace('%s','{detail}') in keys, error
print(f'DCC validation coverage: {len(errors)} distinct producers cataloged')
