"""Independent 90-digit oracle and strict JSON audit; no third-party packages."""
import decimal
import hashlib
import itertools
import json
import pathlib
import platform
import re
import subprocess
import sys

decimal.getcontext().prec = 90
D = decimal.Decimal
driver, output, *driver_args = sys.argv[1:]
cases = []
maximum_error = D(0)
metric_errors = {name: D(0) for name in ('probability', 'log_probability', 'log_evidence', 'entropy', 'scaled')}

for fixture in range(19):
    weights = [D(1 + ((fixture + 3) * (i + 5) * (i + 11)) % 997) for i in range(12)]
    # Includes a joint distribution with small masses and correlated observations.
    if fixture == 15:
        weights = [D(10) ** (i - 6) for i in range(12)]
    logs = [w.ln() for w in weights]
    if fixture in (16, 17):
        logs = [D(10000 if fixture == 16 else -10000) - i for i in range(12)]
        weights = [value.exp() for value in logs]
    if fixture == 18:
        logs = [D(-10000 if (i // 2) % 2 else 0) + D(i // 4) for i in range(12)]
        weights = [value.exp() for value in logs]
    total = sum(weights)
    for x, y in itertools.product((-1, 0, 1), repeat=2):
        kept = [w if (x < 0 or (i // 2) % 2 == x) and (y < 0 or i % 2 == y) else D(0)
                for i, w in enumerate(weights)]
        mass = sum(kept)
        expected = [sum(kept[i:i + 4]) / mass for i in (0, 4, 8)]
        references = [(mass / total).ln(), -sum(p * p.ln() for p in expected)]
        references.extend(v for p in expected for v in (p, p.ln()))
        payload = '\n'.join(map(str, logs)) + f'\n{x} {y}\n'
        run = subprocess.run([driver, *driver_args], input=payload, text=True, capture_output=True, check=True)
        fields = run.stdout.split()
        bits = int(fields[0])
        tolerance = D(64) * D(2) ** (1 - bits)
        observed = [D(fields[1]), D(fields[2])] + [D(v) for v in fields[6:]]
        if len(observed) != len(references):
            raise AssertionError('oracle driver output shape mismatch')
        errors = [abs(a - b) for a, b in zip(observed, references)]
        for error, reference in zip(errors, references):
            if error > tolerance * max(D(1), abs(reference)):
                raise AssertionError(f'fixture {fixture}, context {x,y}: {error} exceeds tolerance')
        selected = 'ABC'[max(range(3), key=expected.__getitem__)]
        if fields[5] != selected:
            raise AssertionError('selected candidate differs from rational oracle')
        if D(fields[3]) > tolerance:
            raise AssertionError('probability normalization residual exceeds tolerance')
        underflows = sum(observed[2 + 2 * i] == 0 and observed[3 + 2 * i].is_finite() for i in range(3))
        if int(fields[4]) != underflows:
            raise AssertionError('underflow diagnostics disagree with returned log probabilities')
        maximum_error = max(maximum_error, *errors)
        metric_errors['log_evidence'] = max(metric_errors['log_evidence'], errors[0])
        metric_errors['entropy'] = max(metric_errors['entropy'], errors[1])
        metric_errors['probability'] = max(metric_errors['probability'], *errors[2::2])
        metric_errors['log_probability'] = max(metric_errors['log_probability'], *errors[3::2])
        metric_errors['scaled'] = max(metric_errors['scaled'],
            *(error / max(D(1), abs(reference)) for error, reference in zip(errors, references)))
        cases.append({'id': f'{fixture}:{x}:{y}', 'joint_weights': list(map(str, weights)),
                      'joint_log_weights': list(map(str, logs)),
                      'observations': {'x': x, 'y': y}, 'expected': list(map(str, references)),
                      'actual': list(map(str, observed)), 'absolute_errors': list(map(str, errors)),
                      'normalization_error': fields[3], 'underflowed_candidates': int(fields[4]),
                      'decision': selected})

root = pathlib.Path(__file__).resolve().parents[1]
sources = sorted(p for folder in ('src', 'tests') for p in (root / folder).rglob('*')
                 if p.is_file() and 'build' not in p.parts and '__pycache__' not in p.parts)
sources.append(root / 'CMakeLists.txt')
cache = next(parent / 'CMakeCache.txt' for parent in
             (pathlib.Path(driver).resolve().parent, *pathlib.Path(driver).resolve().parents)
             if (parent / 'CMakeCache.txt').exists())
configuration = {}
for line in cache.read_text().splitlines():
    if '=' in line and ':' in line and not line.startswith(('//', '#')):
        key = line.split(':', 1)[0]
        if key in ('BUILD_SHARED_LIBS', 'CMAKE_BUILD_TYPE', 'CMAKE_CXX_COMPILER', 'CMAKE_CXX_FLAGS',
                   'MLX_DIR', 'ONNXRuntime_LIBRARY', 'ONNXRuntime_INCLUDE_DIR', 'Vulkan_LIBRARY') or key.startswith('IIDECISION_'):
            configuration[key] = line.split('=', 1)[1]
version = re.search(r'project\(iiDecision VERSION (\S+)', (root / 'CMakeLists.txt').read_text()).group(1)
report = {'version': version, 'reference_precision_digits': 90, 'mantissa_bits': bits,
          'platform': platform.platform(), 'cases': cases, 'case_count': len(cases),
          'maximum_absolute_error': str(maximum_error), 'relative_tolerance': str(tolerance),
          'maximum_errors_by_metric': {k: str(v) for k, v in metric_errors.items()},
          'numeric_fields': ['log_evidence', 'entropy', 'p_A', 'log_p_A', 'p_B', 'log_p_B', 'p_C', 'log_p_C'],
          'build_configuration': configuration,
          'driver_sha256': hashlib.sha256(pathlib.Path(driver).read_bytes()).hexdigest(),
          'driver_args': driver_args,
          'source_sha256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
pathlib.Path(output).write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False) + '\n')
print(f'{len(cases)} independent Decimal cases passed; mantissa={bits}; max absolute error={maximum_error}')
