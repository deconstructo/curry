#!/usr/bin/env python3
"""Jupyter kernel rich display of a cell's result value, over the real
ZeroMQ protocol.

Usage: test_jupyter_display.py <curry_jupyter-binary>
"""
import sys

from jupyter_test_support import Checks, running_kernel


def main():
    t = Checks()
    with running_kernel(sys.argv[1]) as k:
        status, data, _ = k.execute("(define x (sym-var 'x)) (/ x 2)")
        t.check("a symbolic result runs", status, "ok")
        t.check("symbolic result keeps text/plain", "text/plain" in data, True)
        t.check("symbolic result adds text/latex", data.get("text/latex"), "$$\\frac{x}{2}$$")

        _, data, _ = k.execute("(+ (expt x 2) (* 2 x) 1)")
        _, _, plain = k.execute("(display (sym->latex (+ (expt x 2) (* 2 x) 1)))")
        t.check("a polynomial is typeset exactly as sym->latex writes it",
                data.get("text/latex"), "$$" + plain + "$$")

        _, data, _ = k.execute("(∂ (expt x 3) x)")
        t.check("a derivative is typeset", data.get("text/latex"), "$$3 x^{2}$$")

        _, data, _ = k.execute("x")
        t.check("a bare symbolic variable is typeset", data.get("text/latex"), "$$x$$")

        _, data, _ = k.execute("(+ 1 2)")
        t.check("a plain number has only text/plain", sorted(data), ["text/plain"])
        t.check("a plain number's text", data["text/plain"], "3")

        _, data, _ = k.execute('"hello"')
        t.check("a string has only text/plain", sorted(data), ["text/plain"])

        _, data, _ = k.execute("(list x 1)")
        t.check("a list holding a symbolic value has only text/plain", sorted(data), ["text/plain"])

        _, data, out = k.execute('(display "side effect") (/ x 3)')
        t.check("stdout is unaffected", out, "side effect")
        t.check("rich output follows a side effect", data.get("text/latex"), "$$\\frac{x}{3}$$")

        status, data, _ = k.execute("(define y 1)")
        t.check("a definition publishes no result", (status, data), ("ok", None))
    return t.finish()


if __name__ == "__main__":
    sys.exit(main())
