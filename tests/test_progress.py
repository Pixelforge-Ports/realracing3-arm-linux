import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("eapx", ROOT / "tools/eapx.py")
eapx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eapx)

class UI:
    def __init__(self): self.messages, self.bars = [], []
    def send(self, *args): self.messages.append(args)
    def progress(self, *args, **kwargs): self.bars.append((args, kwargs))

class ProgressTests(unittest.TestCase):
    def test_all_phases_and_final_completion(self):
        ui = UI()
        p = eapx.Progress(None, None, tty="none", portmaster=ui)
        p.update(overall=20, message="LOOKING FOR GAME DATA", force=True)
        self.assertEqual(ui.bars[-1][0][1:], (20, 1000))
        p.total_bytes = p.done_bytes = 4096
        p.update(overall=850, message="VALIDATING GAME DATA", force=True)
        self.assertEqual(ui.bars[-1][0][1:], (850, 1000))
        self.assertIn("85%", ui.bars[-1][0][0])
        p.done()
        self.assertEqual(ui.bars[-1][0][1:], (1000, 1000))
        self.assertTrue(ui.bars[-1][1]["force"])
    def test_failure_does_not_report_completion(self):
        ui = UI()
        p = eapx.Progress(None, None, tty="none", portmaster=ui)
        p.fail("Invalid donor")
        self.assertFalse(ui.bars)
        self.assertIn(("message", "Setup failed: Invalid donor"), ui.messages)
    def test_console_only_is_allowed(self):
        p = eapx.Progress(None, None, tty="none", portmaster=None)
        p.update(overall=500, message="EXTRACTING", force=True)
        p.done()
        self.assertEqual(p.last[0:2], (3, 1000))
if __name__ == "__main__": unittest.main()
