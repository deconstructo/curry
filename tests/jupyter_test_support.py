"""Shared scaffolding for the Jupyter kernel tests: start the real kernel
over ZeroMQ and count PASS/FAIL checks."""
import json
import os
import tempfile
from contextlib import contextmanager

from jupyter_client.manager import KernelManager


class Checks:
    def __init__(self):
        self.total = 0
        self.failed = 0

    def check(self, label, got, expected):
        self.total += 1
        if got != expected:
            self.failed += 1
            print("FAIL: %s\n  got      %r\n  expected %r" % (label, got, expected))
        else:
            print("PASS: " + label)

    def finish(self):
        print("\n%d passed, %d failed" % (self.total - self.failed, self.failed))
        return 1 if self.failed else 0


class Kernel:
    def __init__(self, kc):
        self.kc = kc

    def execute(self, code):
        """Run a cell; return (status, execute_result data or None, streamed stdout)."""
        msg_id = self.kc.execute(code)
        result, stdout = None, ""
        while True:
            m = self.kc.get_iopub_msg(timeout=60)
            if m["parent_header"].get("msg_id") != msg_id:
                continue
            t, c = m["msg_type"], m["content"]
            if t == "execute_result":
                result = c["data"]
            elif t == "stream" and c["name"] == "stdout":
                stdout += c["text"]
            elif t == "status" and c["execution_state"] == "idle":
                break
        while True:
            m = self.kc.get_shell_msg(timeout=60)
            if m["parent_header"].get("msg_id") == msg_id:
                return m["content"]["status"], result, stdout

    def complete(self, code, pos=None):
        self.kc.complete(code, len(code) if pos is None else pos)
        return self.kc.get_shell_msg(timeout=30)["content"]

    def inspect(self, code, pos):
        self.kc.inspect(code, pos)
        return self.kc.get_shell_msg(timeout=30)["content"]


@contextmanager
def running_kernel(binary):
    """Start `binary` (curry_jupyter) under a throwaway kernelspec."""
    binary = os.path.abspath(binary)
    with tempfile.TemporaryDirectory() as tmp:
        spec = os.path.join(tmp, "kernels", "curry")
        os.makedirs(spec)
        with open(os.path.join(spec, "kernel.json"), "w") as f:
            json.dump({"argv": [binary, "-f", "{connection_file}"],
                       "display_name": "curry", "language": "scheme"}, f)
        os.environ["JUPYTER_PATH"] = tmp
        km = KernelManager(kernel_name="curry")
        km.start_kernel()
        kc = km.client()
        kc.start_channels()
        try:
            kc.wait_for_ready(timeout=60)
            yield Kernel(kc)
        finally:
            kc.stop_channels()
            km.shutdown_kernel(now=True)
