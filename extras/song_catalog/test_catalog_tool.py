"""Host checks: no ESP32, serial port or real SD card involved."""
import json
from pathlib import Path
import tempfile
import unittest

from catalog_tool import build_catalog, parse_name, write_catalog


class CatalogTests(unittest.TestCase):
    def test_names(self):
        self.assertEqual(parse_name(Path("1-Ethel_Cain-Janie.mp3")), (1, "Ethel Cain", "Janie"))
        self.assertEqual(parse_name(Path("0012-Ethel_Cain-A_Title-Part_2.mp3")),
                         (12, "Ethel Cain", "A Title-Part 2"))

    def test_invalid_names(self):
        for name in ("0001.mp3", "0-A-B.mp3", "11-A-B.mp3", "1--B.mp3", "1-A-.mp3"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                parse_name(Path(name))

    def test_numeric_sort_and_ignored_files(self):
        with tempfile.TemporaryDirectory() as root:
            folder = Path(root)
            for name in ("10-A-Ten.mp3", "2-B-Two.mp3", "1-C-One.MP3", "0011.mp3",
                         "._0001.mp3", ".DS_Store", "notes.txt"):
                (folder / name).touch()
            before = {p.name: p.read_bytes() for p in folder.iterdir()}
            catalog = build_catalog(folder, lambda _: 300000)
            self.assertEqual([t["id"] for t in catalog["tracks"]], [1, 2, 10])
            self.assertEqual(catalog["tracks"][0]["playback_filename"], "0001-C-One.mp3")
            self.assertEqual(catalog["tracks"][0]["duration_ms"], 300000)
            self.assertEqual(before, {p.name: p.read_bytes() for p in folder.iterdir()})

    def test_duplicate_number(self):
        with tempfile.TemporaryDirectory() as root:
            for name in ("1-A-One.mp3", "0001-B-Other.mp3"):
                (Path(root) / name).touch()
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                build_catalog(root, lambda _: 1000)

    def test_bad_media_duration(self):
        with tempfile.TemporaryDirectory() as root:
            (Path(root) / "1-A-One.mp3").touch()
            with self.assertRaisesRegex(ValueError, "Invalid duration"):
                build_catalog(root, lambda _: 0)

    def test_output_safety(self):
        with tempfile.TemporaryDirectory() as root:
            folder = Path(root) / "music"
            folder.mkdir()
            with self.assertRaises(ValueError):
                write_catalog({}, folder / "catalog.json", folder)
            output = Path(root) / "catalog.json"
            write_catalog({"tracks": []}, output, folder)
            self.assertEqual(json.loads(output.read_text()), {"tracks": []})
            with self.assertRaises(FileExistsError):
                write_catalog({}, output, folder)


if __name__ == "__main__":
    unittest.main()
