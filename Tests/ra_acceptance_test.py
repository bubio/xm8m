"""Evidence safety regressions; never launches a GUI or calls the network."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import urllib.error
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('acceptance', Path(__file__).resolve().parents[1] / 'scripts/ra_acceptance.py')
ra = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ra)


class AcceptanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'xm8'
        self.build.write_bytes(b'fixture executable, not a real build')
        self.directory = self.root / 'run'
        self.run = ra.init_run(self.directory, 'macos', self.build, 'fixture', 'test')
        self.case_id = '50-01/Hardcore/dnd-library'
        self.obs = ra.template(self.run, self.case_id)
        self.obs.update(input_methods=['mouse'], media=['ys-en:bank0'], before='Ys identified', after='Ys running')
        self.evidence = self.directory / 'evidence' / 'observation.txt'
        self.evidence.write_text('Synthetic test evidence. Not a GUI acceptance result.')
        case = ra.expand(self.run['catalog'])[self.case_id]
        for name, expected in case['checks'].items():
            self.obs['checks'][name] = {'actual': expected, 'evidence': ['evidence/observation.txt']}

    def save(self):
        path = ra.record(self.directory, self.obs)
        return ra.read(path)

    def test_recorded_success_and_input_coverage(self):
        self.assertEqual(ra.outcome(self.directory, self.run, self.save()), 'PASS')
        self.assertIn('mouse・keyboard', ra.report(self.directory))

    def test_partial_measurement_remains_pending(self):
        self.obs['checks']['drop_reset_count'] = {'actual': None, 'evidence': []}
        self.assertEqual(ra.outcome(self.directory, self.run, self.save()), 'PENDING')

    def test_failure_wins_over_missing_measurement(self):
        self.obs['checks']['drop_reset_count']['actual'] = 2
        self.obs['checks']['ra_hard_retained'] = {'actual': None, 'evidence': []}
        self.assertEqual(ra.outcome(self.directory, self.run, self.save()), 'FAIL')

    def test_foreign_context_rejected(self):
        for field in ('run_id', 'platform', 'build_sha256', 'catalog_sha256'):
            with self.subTest(field=field):
                obs = copy.deepcopy(self.obs)
                obs['binding'][field] = 'different'
                with self.assertRaises(ValueError):
                    ra.record(self.directory, obs)

    def test_automated_test_cannot_be_gui_evidence(self):
        self.obs['operator'] = 'ci'
        with self.assertRaises(ValueError):
            self.save()

    def test_network_case_requires_human(self):
        obs = ra.template(self.run, '46-network/Hardcore/user-operated')
        obs.update(state='PENDING', reason='Needs user')
        with self.assertRaises(ValueError):
            ra.record(self.directory, obs)
        obs['operator'] = 'human'
        ra.record(self.directory, obs)

    def test_boolean_is_not_reset_count(self):
        self.obs['checks']['drop_reset_count']['actual'] = True
        with self.assertRaises(ValueError):
            self.save()

    def test_missing_and_changed_evidence_rejected(self):
        attempt = self.save()
        self.evidence.write_text('changed')
        with self.assertRaises(ValueError):
            ra.outcome(self.directory, self.run, attempt)
        self.evidence.unlink()
        with self.assertRaises(OSError):
            ra.outcome(self.directory, self.run, attempt)

    def test_path_escape_rejected(self):
        self.obs['checks']['drop_reset_count']['evidence'] = ['../xm8']
        with self.assertRaises(ValueError):
            self.save()

    def test_build_replaced_rejected(self):
        self.build.write_bytes(b'different build')
        with self.assertRaises(ValueError):
            self.save()

    def test_ai_advice_cannot_change_result(self):
        self.obs['checks']['drop_reset_count']['actual'] = 2
        self.save()
        ra.write_new(self.directory / 'advice' / 'fake.json', {'choice': 'consistent', 'confidence': 1})
        self.assertIn('| FAIL |', ra.report(self.directory))

    def test_payload_omits_private_run_metadata(self):
        payload = ra.advice_payload(self.run, self.case_id, '未実施')
        serialized = json.dumps(payload)
        self.assertNotIn(str(self.build), serialized)
        self.assertNotIn('device', payload['state'])
        self.assertNotIn('media', payload['state'])

    def test_history_keeps_failed_attempt(self):
        self.obs['checks']['drop_reset_count']['actual'] = 2
        self.save()
        self.obs['checks']['drop_reset_count']['actual'] = 1
        self.save()
        result = ra.report(self.directory)
        self.assertIn(': FAIL', result)
        self.assertIn(': PASS', result)

    def test_four_platforms_initialize_separately(self):
        for platform in ra.PLATFORMS:
            with self.subTest(platform=platform):
                run = ra.init_run(self.root / platform, platform, self.build, 'fixture', platform)
                self.assertEqual(run['platform'], platform)
                self.assertNotEqual(run['run_id'], self.run['run_id'])

    def test_no_api_key_is_clear_failure(self):
        with patch.dict('os.environ', {}, clear=True):
            with self.assertRaisesRegex(ValueError, 'TYPESAFE_API_KEY'):
                ra.ask_typesafe({})

    def test_api_http_failure_never_becomes_pass(self):
        with patch.dict('os.environ', {'TYPESAFE_API_KEY': 'synthetic-key'}):
            with patch.object(ra.urllib.request, 'build_opener') as opener:
                opener.return_value.open.side_effect = urllib.error.HTTPError(
                    'https://api.typesafe.ai/v1/systemone', 401, 'Unauthorized', {}, None)
                with self.assertRaisesRegex(ValueError, 'HTTP 401'):
                    ra.ask_typesafe({})
        self.assertEqual(list((self.directory / 'attempts').iterdir()), [])

    def test_api_timeout_is_not_an_acceptance_result(self):
        with patch.dict('os.environ', {'TYPESAFE_API_KEY': 'synthetic-key'}):
            with patch.object(ra.urllib.request, 'build_opener') as opener:
                opener.return_value.open.side_effect = TimeoutError()
                with self.assertRaisesRegex(ValueError, 'unavailable'):
                    ra.ask_typesafe({})

    def test_credentials_are_not_redirected(self):
        with self.assertRaisesRegex(ValueError, 'redirects'):
            ra.NoRedirect().redirect_request(None, None, 302, '', {}, 'https://other.invalid/')

    def test_report_identifies_missing_measurement(self):
        self.obs['checks']['drop_reset_count'] = {'actual': None, 'evidence': []}
        self.save()
        self.assertIn('drop_reset_count: expected=1, actual=None', ra.report(self.directory))


if __name__ == '__main__':
    unittest.main()
