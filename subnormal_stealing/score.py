"""Offline scoring: compares the attacker's recorded guesses against ground
truth, WITHOUT the attacker page itself ever touching ground truth.

The attacker page (attacker.js) only ever POSTs its guesses to
results_log.jsonl via the attacker-origin server; it never reads
truth_<run_id>.json or GET /truth.json on the victim origin. This script is
the separate, after-the-fact grader a researcher runs once a trial is done.

Usage:
  python3 score.py                      # scores every run found in this dir
  python3 score.py --run-id <run_id>    # scores one specific run
  python3 score.py --log other.jsonl    # point at a results log elsewhere
"""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def load_truth(run_id):
    path = ROOT / f'truth_{run_id}.json'
    if not path.exists():
        raise SystemExit(f'No truth file for run {run_id!r} (expected {path}).')
    return json.loads(path.read_text(encoding='utf-8'))


def score_record(record):
    truth = load_truth(record['runId'])
    pixels = truth['pixels']
    size = truth['size']
    scan = record.get('scan') or []
    if not scan:
        return None

    correct = 0
    wrong = []
    for entry in scan:
        idx = entry['y'] * size + entry['x']
        actual = 'white' if pixels[idx] else 'black'
        guess = entry['guess']
        if guess == actual:
            correct += 1
        else:
            wrong.append({'x': entry['x'], 'y': entry['y'], 'guess': guess, 'actual': actual, 'mean_ms': entry['mean']})

    calibration = record.get('calibration') or {}
    return {
        'runId': record['runId'],
        'userAgent': record.get('userAgent'),
        'calibration_delta_ms': calibration.get('delta'),
        'threshold_ms': calibration.get('threshold'),
        'pixels_scanned': len(scan),
        'correct': correct,
        'accuracy': correct / len(scan),
        'errors': wrong,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', default=str(ROOT / 'results_log.jsonl'))
    parser.add_argument('--run-id', default=None)
    args = parser.parse_args()

    log_path = Path(args.log)
    if not log_path.exists():
        raise SystemExit(f'No results log at {log_path}.')

    any_scored = False
    with log_path.open(encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            record = json.loads(line)
            if args.run_id and record.get('runId') != args.run_id:
                continue
            result = score_record(record)
            if result is None:
                continue
            any_scored = True
            print(f"run {result['runId']}  ua={result['userAgent']!r}")
            print(f"  calibration delta: {result['calibration_delta_ms']} ms  threshold: {result['threshold_ms']} ms")
            print(f"  accuracy: {result['correct']}/{result['pixels_scanned']} = {result['accuracy']:.1%}")
            if result['errors']:
                print(f"  wrong: {result['errors']}")
            print()

    if not any_scored:
        print('No scan records found (scan list was empty, or --run-id matched nothing).')


if __name__ == '__main__':
    main()
