#!/usr/bin/env python3
"""Negative catalog tests for duplicate keys and terminal-control formatting."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import sys

root=Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'tools'))
loader=importlib.util.spec_from_file_location('generate_localizations',
                                            root/'tools/generate_localizations.py')
module=importlib.util.module_from_spec(loader)
loader.loader.exec_module(module)
module.render(root)

with tempfile.TemporaryDirectory(prefix='irc-l10n-generator-') as name:
    path=Path(name); folder=path/'i18n'; folder.mkdir()
    (path/'src').mkdir()
    shutil.copyfile(root/'src/idle_commands.c', path/'src/idle_commands.c')
    (path/'tools').mkdir()
    shutil.copyfile(root/'tools/generated_manual_keys.json', path/'tools/generated_manual_keys.json')
    shutil.copyfile(root/'tools/manual_paragraph_keys.json', path/'tools/manual_paragraph_keys.json')
    shutil.copyfile(root/'tools/context_help_keys.json', path/'tools/context_help_keys.json')
    for entry in (root/'i18n').glob('*.json'):
        shutil.copyfile(entry,folder/entry.name)
    translated=folder/'id.json'
    original=translated.read_text(encoding='utf-8')
    def must_reject(data, description):
        translated.write_text(data,encoding='utf-8')
        try: module.render(path)
        except (ValueError, UnicodeError): return
        raise AssertionError(description+' was accepted by the generator')
    marker='  "Help": '
    assert marker in original
    must_reject(original.replace(marker,'  "Help": "duplicate",\n'+marker,1),
                'duplicate JSON key')
    parsed=json.loads(original)
    for description, replacement in [('bidi override','\u202e'),
                                     ('format injection','%s'),
                                     ('oversized translation','A'*257)]:
        modified=dict(parsed); modified['Help']=replacement
        must_reject(json.dumps(modified,ensure_ascii=False),description)
    help_key = 'Choosing any panel does not switch your chat conversation.'
    modified=dict(parsed); modified[help_key]='A'*1025
    must_reject(json.dumps(modified,ensure_ascii=False), 'oversized help paragraph')
    # The upper help boundary is accepted, while short labels stay limited.
    modified=dict(parsed); modified[help_key]='A'*1024
    translated.write_text(json.dumps(modified,ensure_ascii=False),encoding='utf-8')
    module.render(path)
    translated.write_text(original,encoding='utf-8')
    module.render(path)
print('localization generator: duplicate keys, format, control, and length rejects PASS')
