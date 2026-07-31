window.BENCHMARK_DATA = {
  "lastUpdate": 1785501936859,
  "repoUrl": "https://github.com/deconstructo/curry",
  "entries": {
    "Benchmark": [
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "committer": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "distinct": true,
          "id": "64f6bd66263ad563aa099f9be90a31a5cd069d9f",
          "message": "Fix benchmark.yml CI: permissions: doesn't support expressions\n\nEvery push to this repo since the benchmark workflow was added has\nrun it with zero jobs (\"This run likely failed because of a workflow\nfile issue\" in the Actions UI, confirmed via `gh api\n.../actions/runs/<id>/jobs` returning {\"total_count\":0,\"jobs\":[]} on\nevery single run) - the benchmark suite has never actually executed\nin CI.\n\nRoot cause: the job's permissions block used an expression to pick\nbetween read and write access,\n  contents: ${{ github.event_name == 'push' && 'write' || 'read' }}\nbut GitHub's own context-availability reference table doesn't list\n`permissions` among the keys that accept ${{ }} expressions.\nPermission values must be static literals; a workflow that uses an\nexpression there is silently rejected wholesale, with no diagnostic\npointing at the actual line.\n\nFixed by granting `contents: write` unconditionally at the job level\nand keeping the push-vs-PR distinction where it already correctly\nlived: the `auto-push:` input to the benchmark-action step (an\nordinary `with:` field, which does support expressions), so a PR run\nstill never actually writes to gh-pages even though the token scope\ntechnically allows it. For a pull_request run from a fork, GitHub\nrestricts GITHUB_TOKEN to read-only regardless of what's granted\nhere; the residual exposure is a same-repo (non-fork) PR run holding\nan unused write scope, which still never exercises the write path.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-07-26T17:37:18+10:00",
          "tree_id": "2f9eb159f1365ecd1bfe5739ea95bb7ca335fe4e",
          "url": "https://github.com/deconstructo/curry/commit/64f6bd66263ad563aa099f9be90a31a5cd069d9f"
        },
        "date": 1785051950716,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 22.274,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.934,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.385,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 35.065,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.196,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 432.293,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 86.193,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 130.947,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.177,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "committer": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "distinct": true,
          "id": "c4d8847d18831f511b7dfb9bdd10247fecf739fe",
          "message": "Update Formula/curry.rb sha256 for v1.11.1",
          "timestamp": "2026-07-27T23:10:14+10:00",
          "tree_id": "1c03073598f8c262e071922497cee2dcfccb08e7",
          "url": "https://github.com/deconstructo/curry/commit/c4d8847d18831f511b7dfb9bdd10247fecf739fe"
        },
        "date": 1785157871821,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.509,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 26.558,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.179,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 31.202,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 204.493,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 396.409,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 84.142,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 129.056,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.113,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "committer": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "distinct": true,
          "id": "e56dc30a88de47cfa83fa81c70b13a859c1e6ffa",
          "message": "Update Formula/curry.rb sha256 for v1.12.0\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-07-28T19:21:35+10:00",
          "tree_id": "cdc8fe5e675826e2b212e1ee841d49d57db93d08",
          "url": "https://github.com/deconstructo/curry/commit/e56dc30a88de47cfa83fa81c70b13a859c1e6ffa"
        },
        "date": 1785230552836,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.045,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 30.319,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.078,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.477,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 193.453,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 447.091,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 89.715,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 130.926,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 100.435,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "committer": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "distinct": true,
          "id": "4b96db5c6153dfdfa7c37cb7f2fed3ae074c8265",
          "message": "Update Formula/curry.rb sha256 for v1.13.0",
          "timestamp": "2026-07-30T21:18:41+10:00",
          "tree_id": "5e57a319d16131d5978fb97950e7b6d4946d0289",
          "url": "https://github.com/deconstructo/curry/commit/4b96db5c6153dfdfa7c37cb7f2fed3ae074c8265"
        },
        "date": 1785410367109,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.948,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.731,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.365,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.26,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.573,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 419.328,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 82.59,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 129.189,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.312,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "Chris Ó Luanaigh",
            "username": "deconstructo"
          },
          "committer": {
            "email": "metanoia@gmail.com",
            "name": "Chris Ó Luanaigh",
            "username": "deconstructo"
          },
          "distinct": true,
          "id": "af19c4c6ccac3f5aa544f07bd5c14757ee3a2b36",
          "message": "fix for compiling tls.c n linux",
          "timestamp": "2026-07-31T02:33:27+10:00",
          "tree_id": "563153546445cf2a6620ec6d3f2f10e6d0bdb5b7",
          "url": "https://github.com/deconstructo/curry/commit/af19c4c6ccac3f5aa544f07bd5c14757ee3a2b36"
        },
        "date": 1785429260171,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.426,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.803,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.027,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.291,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.539,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 427.296,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 83.939,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 131.843,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 98.633,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "Chris Ó Luanaigh",
            "username": "deconstructo"
          },
          "committer": {
            "email": "metanoia@gmail.com",
            "name": "Chris Ó Luanaigh",
            "username": "deconstructo"
          },
          "distinct": true,
          "id": "400bafc85d80a2d57d99e2badcc9477440a7e6c7",
          "message": "fix: intermittent actor/STM crash from unrestored VM state on retry\n\nstm_atomically's retry loop longjmps to its own retry_jmp whenever\nstm_retry() fires (frequent under real contention), bypassing the\nVM-state save/restore SCM_PROTECT provides for exceptions. Every retry\nleaked vm->sp/vm->frame_count, and the next retry's apply_arr call\nbuilt on the already-drifted state — corrupting the VM stack within a\nfew hundred retries and surfacing as a bogus arity error or a straight\nsegfault. Fixed by saving/restoring the same state around the retry\nlongjmp points in both stm_atomically and stm_or_else.\n\nAlso fixes a related but independent latent race: GLOBAL_ENV's frame\nhad no synchronization between frame_grow/frame_hash_rehash and\nconcurrent readers on other actor threads. EnvFrame.version now works\nas a seqlock, with a mutex serializing writers; local (non-shared)\nframes are unaffected.\n\nConfirmed via gdb and an LD_PRELOAD SIGSEGV trap: 0 failures across\n500+ stress runs of tests/actors_tests.scm after the fix, versus a\nconsistent ~25-40% failure rate before.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-07-31T20:44:28+10:00",
          "tree_id": "f4575a56dafef5bc0761e7d2bd4d153e57986617",
          "url": "https://github.com/deconstructo/curry/commit/400bafc85d80a2d57d99e2badcc9477440a7e6c7"
        },
        "date": 1785494726965,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.497,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 27.496,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.34,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 32.408,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.642,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 419.706,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 86.13,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 131.99,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 106.626,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "committer": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "distinct": true,
          "id": "0ea48609b4f76497f9758b8d77012a2fded57d8d",
          "message": "Release v1.14.0",
          "timestamp": "2026-07-31T22:43:26+10:00",
          "tree_id": "da93f01d4328054fb66306d11870d78d5dee1be0",
          "url": "https://github.com/deconstructo/curry/commit/0ea48609b4f76497f9758b8d77012a2fded57d8d"
        },
        "date": 1785501862153,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.416,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.358,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.108,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.271,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.331,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 340.401,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.889,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.945,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.298,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "committer": {
            "email": "metanoia@gmail.com",
            "name": "Scáth",
            "username": "deconstructo"
          },
          "distinct": true,
          "id": "9d76e0510e21e7fe03bfb2eab38275e7014547d4",
          "message": "Update Formula/curry.rb sha256 for v1.14.0",
          "timestamp": "2026-07-31T22:44:45+10:00",
          "tree_id": "d79416a90e55d0672eae0bfcc28f216ff51c9cac",
          "url": "https://github.com/deconstructo/curry/commit/9d76e0510e21e7fe03bfb2eab38275e7014547d4"
        },
        "date": 1785501935550,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.858,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 32.991,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.232,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.442,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.364,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 373.595,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.717,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.652,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.672,
            "unit": "ms"
          }
        ]
      }
    ]
  }
}