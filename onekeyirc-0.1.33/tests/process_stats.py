"""Identity-checked Linux /proc samples for a directly spawned test process.

A sandbox may return namespace-local PIDs while exposing the host's /proc.
Never index /proc with Popen.pid without checking PPid and NSpid. This helper
has no application/runtime role and does not measure the Python harness.
"""
import os
from pathlib import Path
import re
import sys


class ProcessStatsError(RuntimeError):
    pass


_CACHE = {}


def _status(path):
    # comm/Name may contain arbitrary bytes. Identity and metric fields are
    # ASCII numbers, so an unrelated Unicode process name must not abort a
    # directory scan or weaken validation of those numeric fields.
    text = path.read_text(encoding='ascii', errors='replace')
    result = {}
    for line in text.splitlines():
        key, separator, value = line.partition(':')
        if separator: result[key] = value.strip()
    return result


def _numbers(status, key):
    try:
        return tuple(int(value) for value in status[key].split())
    except (KeyError, ValueError) as error:
        raise ProcessStatsError('Missing or invalid /proc %s identity field' % key) from error


def _parent_identity():
    if not sys.platform.startswith('linux'):
        raise ProcessStatsError('Verified /proc sampling is available only on Linux')
    try:
        status = _status(Path('/proc/self/status'))
        outer = _numbers(status, 'Pid')[0]
        namespace = _numbers(status, 'NSpid') if 'NSpid' in status else (outer,)
    except OSError as error:
        raise ProcessStatsError('Cannot read this sampler process identity: %s' % error) from error
    if namespace[-1] != os.getpid():
        raise ProcessStatsError('/proc/self NSpid does not identify this sampler namespace')
    return outer, len(namespace)


def _matches(status, pid, parent, depth):
    if _numbers(status, 'PPid')[0] != parent:
        return False
    namespace = _numbers(status, 'NSpid') if 'NSpid' in status else _numbers(status, 'Pid')
    return len(namespace) == depth and namespace[-1] == pid


def _discover(pid, parent, depth):
    key = (parent, pid)
    cached = _CACHE.get(key)
    if cached is not None:
        try:
            status = _status(Path('/proc') / str(cached) / 'status')
            if _matches(status, pid, parent, depth):
                return cached, status
        except (OSError, ProcessStatsError):
            pass
        _CACHE.pop(key, None)
    try:
        entries = list(Path('/proc').iterdir())
    except OSError as error:
        raise ProcessStatsError('Cannot enumerate /proc for verified child lookup: %s' % error) from error
    for entry in entries:
        if not entry.name.isdigit():
            continue
        try:
            status = _status(entry / 'status')
            if _matches(status, pid, parent, depth):
                outer = int(entry.name)
                _CACHE[key] = outer
                return outer, status
        except (OSError, ProcessStatsError):
            # Processes may disappear while the directory is being scanned.
            continue
    raise ProcessStatsError('No verified direct child for namespace PID %d (outer parent %d)' % (pid, parent))


def _stat(path):
    text = (path / 'stat').read_text(encoding='ascii', errors='replace')
    # comm may contain spaces and parentheses; fields after the last closing
    # parenthesis have the documented fixed positions starting with state.
    fields = text.rsplit(') ', 1)[1].split()
    return {'state': fields[0], 'parent': int(fields[1]),
            'user_ticks': int(fields[11]), 'system_ticks': int(fields[12]),
            'start_ticks': int(fields[19]), 'rss_pages': int(fields[21])}


def _children(parent):
    children = []
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            status = _status(entry / 'status')
            if _numbers(status, 'PPid')[0] != parent:
                continue
            namespace = _numbers(status, 'NSpid') if 'NSpid' in status else _numbers(status, 'Pid')
            children.append({'outer_pid': int(entry.name), 'namespace_pid': namespace[-1],
                             'state': status.get('State', ''), 'name': status.get('Name', '')})
        except (OSError, ProcessStatsError):
            continue
    return sorted(children, key=lambda item:item['outer_pid'])


def sample_child(pid, expected_exe=None, include_children=True):
    """Sample this Python process's direct child, rejecting unverified identity.

    expected_exe is compared with /proc/<outer_pid>/exe where permitted.
    CPU/RSS describe the child itself; direct resolver children are enumerated
    separately. Missing fd/exe access is explicit, never replaced by a guess.
    """
    if not isinstance(pid, int) or pid <= 0:
        raise ProcessStatsError('A positive Popen.pid is required')
    parent, depth = _parent_identity()
    outer, status = _discover(pid, parent, depth)
    path = Path('/proc') / str(outer)
    try:
        first = _stat(path)
        if first['parent'] != parent:
            raise ProcessStatsError('Child parent changed during sampling')
        executable = None
        exe_error = None
        try:
            executable = os.readlink(path / 'exe')
        except OSError as error:
            exe_error = str(error)
        if executable is not None and expected_exe is not None:
            expected = str(Path(expected_exe).resolve())
            if executable != expected:
                raise ProcessStatsError('Child executable mismatch: %r instead of %r' % (executable, expected))
        fd_count = None
        fd_error = None
        try:
            fd_count = len(list((path / 'fd').iterdir()))
        except OSError as error:
            fd_error = str(error)
        children = _children(outer) if include_children else []
        current_status = _status(path / 'status')
        second = _stat(path)
        if (not _matches(current_status, pid, parent, depth) or
                second['start_ticks'] != first['start_ticks']):
            raise ProcessStatsError('Child identity changed during sampling')
        tick_hz = int(os.sysconf('SC_CLK_TCK'))
        page_bytes = int(os.sysconf('SC_PAGE_SIZE'))
        rss = re.fullmatch(r'(\d+) kB', current_status.get('VmRSS', ''))
        rss_value = int(rss[1]) if rss else second['rss_pages'] * page_bytes // 1024
        peak_rss = re.fullmatch(r'(\d+) kB', current_status.get('VmHWM', ''))
        return {'pid': pid, 'namespace_pid': pid, 'outer_pid': outer, 'verified_pid': outer,
                'outer_parent_pid': parent,
                'namespace_depth': depth, 'identity_verified': True,
                'exe': executable, 'exe_verified': expected_exe is not None and executable is not None,
                'exe_error': exe_error, 'state': second['state'], 'start_ticks': second['start_ticks'],
                'rss_kib': rss_value,
                'peak_rss_kib': int(peak_rss[1]) if peak_rss else None,
                'user_cpu_seconds': second['user_ticks'] / tick_hz,
                'system_cpu_seconds': second['system_ticks'] / tick_hz,
                'cpu_seconds': (second['user_ticks'] + second['system_ticks']) / tick_hz,
                'cpu_tick_seconds': 1.0 / tick_hz,
                'fd_count': fd_count, 'fd_error': fd_error,
                'children': children, 'child_count': len(children)}
    except (OSError, ValueError, IndexError) as error:
        raise ProcessStatsError('Verified child disappeared or /proc sample failed: %s' % error) from error
