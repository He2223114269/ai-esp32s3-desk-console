import importlib.util
import json
import pathlib
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from unittest.mock import patch


MODULE_PATH = pathlib.Path(__file__).parents[1] / "tools" / "ai_proxy.py"
SPEC = importlib.util.spec_from_file_location("ai_proxy", MODULE_PATH)
ai_proxy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ai_proxy)


class ProxyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ai_proxy.DEVICE_TOKEN = "unit-test-token"
        ai_proxy.API_KEY = "unit-test-key"
        cls.answers = []

        def fake_model(messages):
            cls.answers.append(messages)
            return "test answer"

        cls.model_patch = patch.object(ai_proxy.Handler, "ask_model", staticmethod(fake_model))
        cls.model_patch.start()
        ai_proxy.history.clear()
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), ai_proxy.Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)
        cls.model_patch.stop()

    def test_health(self):
        with urllib.request.urlopen(self.base_url + "/health") as response:
            payload = json.loads(response.read())
        self.assertTrue(payload["ok"])
        self.assertTrue(payload["model_configured"])

    def test_chat_requires_device_token(self):
        request = urllib.request.Request(
            self.base_url + "/chat",
            data=b'{"message":"hello"}',
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with self.assertRaises(urllib.error.HTTPError) as error:
            urllib.request.urlopen(request)
        self.assertEqual(error.exception.code, 401)

    def test_chat_keeps_short_conversation_context(self):
        for text in ("first", "second"):
            request = urllib.request.Request(
                self.base_url + "/chat",
                data=json.dumps({"message": text}).encode(),
                headers={
                    "Authorization": "Bearer unit-test-token",
                    "Content-Type": "application/json",
                },
                method="POST",
            )
            with urllib.request.urlopen(request) as response:
                self.assertEqual(response.read().decode(), "test answer")

        roles_and_text = [(item["role"], item["content"]) for item in self.answers[-1]]
        self.assertIn(("user", "first"), roles_and_text)
        self.assertIn(("assistant", "test answer"), roles_and_text)
        self.assertEqual(roles_and_text[-1], ("user", "second"))


if __name__ == "__main__":
    unittest.main()
