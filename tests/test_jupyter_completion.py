#!/usr/bin/env python3
"""Jupyter kernel completion and inspect, over the real ZeroMQ protocol.

Usage: test_jupyter_completion.py <curry_jupyter-binary>

Needs jupyter_client; tests/CMakeLists.txt registers this only when the
kernel was built and a Python with jupyter_client was found.
"""
import sys

from jupyter_test_support import Checks, running_kernel


def main():
    t = Checks()
    with running_kernel(sys.argv[1]) as k:
        status, _, _ = k.execute("(define (my-square-root x) (sqrt x)) (define counter 0)")
        t.check("define in an earlier cell", status, "ok")

        r = k.complete("(string-ap")
        t.check("complete builtin", r["matches"], ["string-append"])
        t.check("complete replaces the token (start, end)",
                (r["cursor_start"], r["cursor_end"]), (1, 10))
        t.check("complete a name from an earlier cell",
                k.complete("(my-sq")["matches"], ["my-square-root"])
        t.check("complete a global variable", k.complete("(display cou")["matches"], ["counter"])
        t.check("complete a local binding in unfinished text",
                k.complete("(let ((tmpval 1)) (+ tmpval tmp")["matches"], ["tmpval"])
        t.check("complete a local bound inside an unclosed [ ] let",
                "bx" in k.complete("(let ([bx 1]) (+ bx b")["matches"], True)
        t.check("complete with the cursor still inside an unclosed [",
                "bx" in k.complete("(let ([bx 1 (+ bx b")["matches"], True)
        neg = k.complete("(string-ap", -5)
        t.check("a negative cursor is clamped, not echoed back",
                (neg["cursor_start"], neg["cursor_end"]) >= (0, 0), True)
        mid = k.complete("(string-append x)", 8)["matches"]
        t.check("complete with the cursor mid-token uses the text before it",
                "string-append" in mid and "string->list" in mid, True)
        t.check("no completions for an empty prefix", k.complete("(", 1)["matches"], [])
        u = k.complete("(define λx 1) (display λ")
        t.check("complete counts positions in code points",
                (u["matches"], u["cursor_start"], u["cursor_end"]), (["λx"], 23, 24))

        i = k.inspect("(string-append a b)", 5)
        t.check("inspect a builtin", (i["found"], i["data"]["text/plain"]),
                (True, "string-append — builtin procedure"))
        t.check("inspect also returns markdown",
                i["data"]["text/markdown"], "**string-append** — builtin procedure")
        t.check("inspect at the end of a token", k.inspect("(map", 4)["found"], True)
        t.check("inspect a name from an earlier cell",
                k.inspect("(my-square-root 4)", 4)["data"]["text/plain"],
                "my-square-root — defined in this session")
        t.check("inspect an unknown name", k.inspect("(no-such-thing 1)", 4)["found"], False)
        t.check("inspect on whitespace", k.inspect("(a  b)", 3)["found"], False)
    return t.finish()


if __name__ == "__main__":
    sys.exit(main())
