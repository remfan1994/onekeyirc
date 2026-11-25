#!/usr/bin/env python3
"""Regression tests for the catalog-level named-field gate."""
from generate_localizations import check_named_fields

check_named_fields("{name}: {count}", "{count} / {name}", "test")
check_named_fields("Manual", "Manuel", "test")
for key, value in [
    ("{name}: {count}", "{name}"),
    ("{name}: {count}", "{name} {count} {count}"),
    ("{name}: {count}", "{name} {count} {extra}"),
    ("{name}: {count}", "{name} {count} {"),
    ("Manual", "{name}"),
]:
    try:
        check_named_fields(key, value, "test")
    except ValueError:
        continue
    raise AssertionError((key, value))
print("Named-field catalog validation: PASS")
