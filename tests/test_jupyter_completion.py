#!/usr/bin/env python3
"""Jupyter kernel completion and inspect, over the real ZeroMQ protocol.

Usage: test_jupyter_completion.py <curry_jupyter-binary>

Needs jupyter_client; tests/CMakeLists.txt registers this only when the
kernel was built and a Python with jupyter_client was found.
"""
import json
import os
import sys
import tempfile

from jupyter_client.manager import KernelManager

fail = 0
total = 0


def check(label, got, expected):
    global fail, total
    total += 1
    if got != expected:
        fail += 1
        print("FAIL: %s\n  got      %r\n  expected %r" % (label, got, expected))
    else:
        print("PASS: " + label)


def main():
    binary = os.path.abspath(sys.argv[1])
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

            def execute(code):
                kc.execute(code)
                while True:
                    m = kc.get_shell_msg(timeout=60)
                    if m["msg_type"] == "execute_reply":
                        return m["content"]["status"]

            def complete(code, pos=None):
                kc.complete(code, len(code) if pos is None else pos)
                return kc.get_shell_msg(timeout=30)["content"]

            def inspect(code, pos):
                kc.inspect(code, pos)
                return kc.get_shell_msg(timeout=30)["content"]

            check("define in an earlier cell",
                  execute("(define (my-square-root x) (sqrt x)) (define counter 0)"), "ok")

            r = complete("(string-ap")
            check("complete builtin", r["matches"], ["string-append"])
            check("complete replaces the token (start, end)",
                  (r["cursor_start"], r["cursor_end"]), (1, 10))
            check("complete a name from an earlier cell",
                  complete("(my-sq")["matches"], ["my-square-root"])
            check("complete a global variable", complete("(display cou")["matches"], ["counter"])
            check("complete a local binding in unfinished text",
                  complete("(let ((tmpval 1)) (+ tmpval tmp")["matches"], ["tmpval"])
            check("complete with the cursor mid-token uses the text before it",
                  "string-append" in complete("(string-append x)", 8)["matches"]
                  and "string->list" in complete("(string-append x)", 8)["matches"], True)
            check("no completions for an empty prefix", complete("(", 1)["matches"], [])
            u = complete("(define λx 1) (display λ")
            check("complete counts positions in code points",
                  (u["matches"], u["cursor_start"], u["cursor_end"]), (["λx"], 23, 24))

            i = inspect("(string-append a b)", 5)
            check("inspect a builtin", (i["found"], i["data"]["text/plain"]),
                  (True, "string-append — builtin procedure"))
            check("inspect also returns markdown",
                  i["data"]["text/markdown"], "**string-append** — builtin procedure")
            check("inspect at the end of a token", inspect("(map", 4)["found"], True)
            check("inspect a name from an earlier cell",
                  inspect("(my-square-root 4)", 4)["data"]["text/plain"],
                  "my-square-root — defined in this session")
            check("inspect an unknown name", inspect("(no-such-thing 1)", 4)["found"], False)
            check("inspect on whitespace", inspect("(a  b)", 3)["found"], False)
        finally:
            kc.stop_channels()
            km.shutdown_kernel(now=True)

    print("\n%d passed, %d failed" % (total - fail, fail))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
