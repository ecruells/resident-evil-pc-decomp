"""Voice importer integration checks; uses ffmpeg when available."""
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest
import wave
import zipfile

spec = importlib.util.spec_from_file_location(
    'voice_importer', Path(__file__).resolve().parents[1] / 'tools/import_voice_clips.py')
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


class VoiceImporterTests(unittest.TestCase):
    def test_missing_coverage_does_not_block_planning(self):
        with tempfile.TemporaryDirectory() as temp:
            destination = Path(temp) / 'voices'
            clip = Path(temp) / 'take.wav'
            rows = [('barry', category, [clip]) for category in importer.CATEGORIES]
            rows += [('jill', 'help', [clip]), ('jill', 'hurt', []),
                     ('jill', importer.SKIP, [clip])]
            missing = importer.missing_categories(rows)
            self.assertNotIn('barry', missing)
            self.assertNotIn('help', missing['jill'])
            self.assertIn('hurt', missing['jill'])
            self.assertEqual(len(missing['jill']), 17)
            self.assertEqual(len(importer.plan(rows, destination)), 19)
            self.assertFalse(destination.exists())
            complete = rows[:18] + [('jill', category, [clip]) for category in importer.CATEGORIES]
            self.assertEqual(importer.missing_categories(complete), {})
            self.assertEqual(len(importer.plan(complete, destination)), 36)

    def test_zip_merge_and_conversion(self):
        ffmpeg = shutil.which('ffmpeg')
        if not ffmpeg:
            self.skipTest('ffmpeg is required for conversion integration')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            recording = root / 'original.wav'
            with wave.open(str(recording), 'wb') as audio:
                audio.setnchannels(2)
                audio.setsampwidth(2)
                audio.setframerate(48000)
                audio.writeframes(b'\x01\x00\x02\x00' * 4800)
            archive = root / 'pack.zip'
            with zipfile.ZipFile(archive, 'w') as pack:
                for name in ('Run away/resident_evil_barry/take10.wav',
                             'Run away/resident_evil_barry/take2.wav',
                             'Flee/arbitrary_speaker_name/take1.wav'):
                    pack.write(recording, name)
            extracted = root / 'source'
            importer.extract_audio(archive, extracted)
            rows = importer.scan(extracted)
            self.assertEqual(len(rows), 2)
            self.assertEqual([str(folder) for folder, _ in rows], ['Flee', 'Run away'])
            assignments = [('barry', importer.suggest(folder.name), clips) for folder, clips in rows]
            destination = root / 'voices'
            jobs = importer.plan(assignments, destination)
            self.assertEqual([target.name for _, target in jobs],
                             ['runaway_1.wav', 'runaway_2.wav', 'runaway_3.wav'])
            self.assertEqual([source.name for source, _ in jobs],
                             ['take1.wav', 'take2.wav', 'take10.wav'])
            for source, target in jobs:
                importer.convert(source, target, ffmpeg)
                with wave.open(str(target), 'rb') as audio:
                    self.assertEqual((audio.getnchannels(), audio.getframerate(), audio.getsampwidth()),
                                     (1, 22050, 2))
                    self.assertGreater(audio.getnframes(), 0)
            with self.assertRaisesRegex(ValueError, 'already exists'):
                importer.plan(assignments, destination)
            self.assertTrue(recording.exists())
            with self.assertRaisesRegex(ValueError, 'supports 16'):
                importer.plan([('barry', 'runaway', [recording] * 17)], root / 'other')

    def test_zip_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / 'bad.zip'
            with zipfile.ZipFile(archive, 'w') as pack:
                pack.writestr('../escape.wav', b'bad')
            with self.assertRaisesRegex(ValueError, 'Unsafe ZIP path'):
                importer.extract_audio(archive, root / 'source')
            self.assertFalse((root / 'escape.wav').exists())


if __name__ == '__main__':
    unittest.main()
