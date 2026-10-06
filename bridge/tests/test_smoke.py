import importlib.util
import pathlib
import socket
import subprocess
import sys
import time
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "smoke.py"

class SmokeToolTest(unittest.TestCase):
    def test_loopback_input_event_and_ack(self):
        # Reserve an available port then launch the actual CLI receiver/sender.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
        listener = subprocess.Popen([sys.executable, str(SCRIPT), "listen", "--port", str(port), "--seconds", "2"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            time.sleep(0.2)
            sent = subprocess.run([sys.executable, str(SCRIPT), "send", "--port", str(port), "--seconds", "0.3", "--tnt"],
                                  capture_output=True, text=True, timeout=4)
            out, err = listener.communicate(timeout=5)
            self.assertEqual(sent.returncode, 0, sent.stderr)
            self.assertEqual(listener.returncode, 0, err)
            self.assertIn("ACK received", sent.stdout)
            self.assertIn("unique events=1", out)
            self.assertNotIn("input packets=0", out)
        finally:
            if listener.poll() is None:
                listener.kill(); listener.communicate()

    def test_receiver_fails_without_input(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]
        result = subprocess.run([sys.executable, str(SCRIPT), "listen", "--port", str(port), "--seconds", "0.1"],
                                capture_output=True, text=True, timeout=3)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("No input received", result.stderr)

if __name__ == "__main__": unittest.main()
