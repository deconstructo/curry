window.BENCHMARK_DATA = {
  "lastUpdate": 1788534601447,
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
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "6bc1a0df222323fea572554f2761a2f1a713b932",
          "message": "Update CHANGELOG for version 1.14.0 release\n\nUpdated the changelog to reflect the release of version 1.14.0.",
          "timestamp": "2026-08-01T12:41:17+10:00",
          "tree_id": "155cce85b852829bfe3289e26ab2f46880969907",
          "url": "https://github.com/deconstructo/curry/commit/6bc1a0df222323fea572554f2761a2f1a713b932"
        },
        "date": 1785552125330,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 22.394,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 36.697,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.959,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 41.743,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 196.558,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 451.122,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 88.57,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 132.414,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.478,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "0981cad9c5f9a13f6f88fbddd8fa4cbd13155962",
          "message": "Modify CHANGELOG for uncommitted updates\n\nUpdated changelog to reflect uncommitted changes.",
          "timestamp": "2026-08-01T12:42:26+10:00",
          "tree_id": "2ed90bfad6a0ae8d849f5bea1774bd3f69562ff9",
          "url": "https://github.com/deconstructo/curry/commit/0981cad9c5f9a13f6f88fbddd8fa4cbd13155962"
        },
        "date": 1785552199886,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.324,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 35.284,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.968,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 40.416,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.057,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 421.005,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 84.471,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 129.255,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.7,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "95e6edf264e577ba02a4de7df9d78594f2820272",
          "message": "Update CHANGELOG for version 1.14.0\n\nUpdated changelog to reflect version 1.14.0 release.",
          "timestamp": "2026-08-01T12:43:24+10:00",
          "tree_id": "155cce85b852829bfe3289e26ab2f46880969907",
          "url": "https://github.com/deconstructo/curry/commit/95e6edf264e577ba02a4de7df9d78594f2820272"
        },
        "date": 1785552257669,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.888,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.6,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.042,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.552,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.736,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 384.73,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.51,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 122.077,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.278,
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
          "id": "b416f654f055b10e3cd367cc1de78a3cf5197b5a",
          "message": "docs: changelog, README module table, CLAUDE.md test-suite table\n\nRecord the number->string/cuneiform-tower fixes and the new\n(curry babylonian-astronomy) module in CHANGELOG.md's Unreleased section;\nadd babylonian-astronomy to README.md's module table; update CLAUDE.md's\ntest-suite table (sexagesimal's assertion count grew from 76 to 110, new\nbabylonian_astronomy row) and the sexagesimal architecture blurb to\nmention the extended cuneiform tower notation and the new module.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-01T18:37:37+10:00",
          "tree_id": "14c7f6bd6f0a18490912d98b6ab64fd4dceed4a6",
          "url": "https://github.com/deconstructo/curry/commit/b416f654f055b10e3cd367cc1de78a3cf5197b5a"
        },
        "date": 1785573570216,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.98,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.738,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.015,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.556,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.842,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 378.292,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 74.427,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 123.192,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.564,
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
          "id": "b34d562e3a259139a9d6163a6837156722ad5d1a",
          "message": "Update Formula/curry.rb sha256 for v1.14.1",
          "timestamp": "2026-08-01T18:43:35+10:00",
          "tree_id": "a170e251251e3c660bd33d466930545ea8de57b1",
          "url": "https://github.com/deconstructo/curry/commit/b34d562e3a259139a9d6163a6837156722ad5d1a"
        },
        "date": 1785573870377,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.202,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.004,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.016,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.773,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.242,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 377.087,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.73,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 122.286,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.432,
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
          "id": "743e9dd5b10146e8ac2a5bdea00b8ad39765fc03",
          "message": "Update CHANGELOG for v1.14.1 release",
          "timestamp": "2026-08-01T18:51:32+10:00",
          "tree_id": "cd370e3efe0e26921d30b04f6c750c48cc077c37",
          "url": "https://github.com/deconstructo/curry/commit/743e9dd5b10146e8ac2a5bdea00b8ad39765fc03"
        },
        "date": 1785574348710,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 23.134,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.624,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.204,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.426,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 211.634,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 365.815,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.47,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 122.737,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 107.127,
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
          "id": "b4a5d941a8d7d219dcf26da9ffc64292b4dd006c",
          "message": "docs: split SRFI docs into individual pages under docs/reference/srfi/\n\nmodule-srfi.md had grown to 449 lines covering 24 SRFI libraries in one\nfile; three others (SRFI-69/90, SRFI-19, SRFI-174) already had separate\ntop-level doc files of their own. Restructures all of it into one page\nper SRFI (paired SRFIs that are documented together and layer on each\nother -- 69+90, 125+126, 132+133 -- share one page) under\ndocs/reference/srfi/, with docs/reference/srfi/index.md as the full\navailability table + bare-number-shim explanation + portability note.\n\nmodule-srfi.md is now a short pointer to srfi/index.md rather than\nduplicating its content. README.md's SRFI table, module-posix.md, and\nmodule-codesets.md updated to link directly to the relevant per-SRFI\npage instead of the old monolithic doc. One stray doc-path reference in\na Scheme source comment (lib/curry/modules/srfi/s19/time.scm) updated\ntoo. No code changes -- docs only.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-01T19:12:23+10:00",
          "tree_id": "600c73bcf5b59046348b7ffffe3dc10a3b23d605",
          "url": "https://github.com/deconstructo/curry/commit/b4a5d941a8d7d219dcf26da9ffc64292b4dd006c"
        },
        "date": 1785575597808,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 23.142,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.168,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.179,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.843,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.619,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 370.644,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.921,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.738,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.526,
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
          "id": "574c6d1d7bb859282855db25e2b7576e770ca987",
          "message": "docs: move README.md's reference tables into docs/reference/\n\nREADME had grown to 349 lines, most of it reference-table material\n(numeric tower, CAS procedures, parallel map/reduce, SRFI-27 random,\nmodules, SRFI compatibility, LLVM JIT benchmarks, FFI types, GC flags,\nAkkadian error phrases) rather than the introductory content a README\nshould lead with.\n\n- Numeric tower table: deleted, already covered by language.md's Values\n  and types table.\n- CAS table: moved into a new \"Quick reference\" section at the top of\n  symbolic.md (which had no consolidated procedure table before).\n- Parallel map/reduce table: moved to new docs/reference/parallel.md.\n- SRFI-27 random table: deleted, already covered by\n  docs/reference/srfi/s27.md.\n- Modules table (36 rows): moved to new docs/reference/modules.md.\n- SRFI compatibility table (21 rows): deleted, already covered by\n  docs/reference/srfi/index.md.\n- LLVM JIT procedure/benchmark tables + build command: moved to new\n  docs/reference/llvm-jit.md.\n- FFI \"Supported types\" line: dropped, already covered in more detail\n  by module-ffi.md's existing Type mapping table.\n- GC backend table + gc-stats field breakdown: deleted, already covered\n  by the existing docs/reference/gc.md.\n- Akkadian error-message table: moved into a new \"Error messages\"\n  section in akkadian-reference.md (genuinely new content there, not a\n  duplicate).\n\nREADME's own Documents index updated with links to all the\nnew/newly-referenced pages. Checked every new/changed relative link\nresolves to a real file (one pre-existing dangling link to a since-removed\ndocs/PHILOSOPHY.md was found but is unrelated to this change, left as-is).\n\nREADME.md: 349 -> 171 lines.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-01T19:24:14+10:00",
          "tree_id": "4981a9944219940cbce9d77078d77c3ca940ec1b",
          "url": "https://github.com/deconstructo/curry/commit/574c6d1d7bb859282855db25e2b7576e770ca987"
        },
        "date": 1785576316577,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 23.384,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.097,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.171,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.125,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 198.292,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 367.888,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.96,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 124.366,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 104.021,
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
          "id": "ce68855210708aa8df85eeb99966391df8b07ee7",
          "message": "docs: sort SRFI index table by increasing SRFI number\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-01T19:28:27+10:00",
          "tree_id": "c80e65c003556246e3eac086b578b74d138e0a00",
          "url": "https://github.com/deconstructo/curry/commit/ce68855210708aa8df85eeb99966391df8b07ee7"
        },
        "date": 1785576680592,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.588,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.044,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.84,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 31.272,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 159.272,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 280.766,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 56.105,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 94.658,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 81.488,
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
          "id": "2dfc1ecee1958415e922785a8138386cd1410c89",
          "message": "feat: (srfi srfi-N) shims for SRFI-261 portable library naming\n\nSRFI-261 (finalized 2025-12-07) specifies (srfi srfi-N) as the primary\nportable library-name form for referring to SRFI N -- distinct from both\nthe bare (srfi N) shorthand curry already ships and curry's own\ndescriptive (srfi sN name) naming, and silent on the latter.\n\nAdded a (srfi srfi-N) shim for all 24 existing numbered SRFIs, identical\nin shape to the existing (srfi N) shims (same underlying (srfi sN name)\nlibrary, one more library-name segment) -- mechanically generated from\nthe existing N.scm shims since the two are byte-identical apart from the\ndefine-library name.\n\nNew tests/srfi_srfi_prefixed_shims_tests.scm mirrors\nsrfi_numbered_shims_tests.scm's checks via the srfi-N form.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-01T19:44:25+10:00",
          "tree_id": "681c9c3794297e1809a6036785516735c52ffc5c",
          "url": "https://github.com/deconstructo/curry/commit/2dfc1ecee1958415e922785a8138386cd1410c89"
        },
        "date": 1785577651140,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.372,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.379,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.122,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.088,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.583,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 345.979,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.668,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.189,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 106.245,
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
          "id": "32b6c8d6856b8858f46c1a6a653f40c3c1b2d30d",
          "message": "feat: (srfi s263 prototype-objects) — SRFI-263 Prototype Object System\n\nA \"Self\"-inspired prototype/message-passing object system: objects are\nplain procedures invoked as (obj 'message . args), holding value/method/\nparent slots in a private table, derived from other objects (cloning-\nwith-a-parent-pointer) rather than instantiated from classes. Complements\n(curry oop), the existing CLOS-style class/generic-function system --\ndifferent paradigms, not competing implementations of the same thing.\n\nImplements *the-root-object*, derive/copy/mirror, set-value-slot!/\nset-method-slot!/set-parent-slot!/delete-slot!, message-not-understood/\nambiguous-message-send (both overridable), slot?/slot-getter/slot-setter/\nslot-type, and the define-method/define-object/derive-object/copy-object\nsyntactic sugar. (srfi 263) and (srfi srfi-263) shims included.\n\nTwo real bugs found and fixed during implementation/review, both in the\nparent-chain search algorithm (%find-slot/%search-parents/%deliver):\n\n1. Diamond-inheritance ambiguity detection didn't dedupe hits by slot\n   identity, so an object with 2+ parents sending any message that\n   resolves via ambiguous-message-send (itself searched across those same\n   parents, converging on the same root handler through every branch)\n   recursed forever instead of raising once. Fixed by deduping search\n   results by slot eq? identity before counting them as distinct.\n\n2. (found by an independent code-review subagent) set-parent-slot! is\n   ordinary public API with no cycle check, so a buggy or adversarial\n   call (most simply an object re-parenting itself) could create a cycle\n   in the parent graph -- every parent-walking search (dispatch, resend,\n   and the mirror's has-ancestor/full-ancestor-list/full-slot-list) had\n   no guard against revisiting an object already on the search path,\n   causing a native stack-overflow segfault. Fixed with a visited-set\n   threaded through all of them; a cycle that additionally severs every\n   path back to *the-root-object* (so even the message-not-understood/\n   ambiguous-message-send fallback handlers become unreachable) now\n   raises a plain Scheme error instead of attempting a fallback that\n   would itself loop.\n\n29-assertion test suite covering both regressions plus the normal API\nsurface. Docs at docs/reference/srfi/s263.md, including the specific\ninterpretive choices made where the SRFI's own prose is thin/example-\ndriven (ambiguity dedup, copy's exact semantics, multi-parent auto-naming,\nthe cycle-safety notes above).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-01T20:27:00+10:00",
          "tree_id": "f04f05cc4852d42ab17b302dffa8e58ccce3002e",
          "url": "https://github.com/deconstructo/curry/commit/32b6c8d6856b8858f46c1a6a653f40c3c1b2d30d"
        },
        "date": 1785580081658,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.373,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.418,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.132,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 40.673,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.137,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 415.349,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 85.853,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 131.924,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 103.067,
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
          "id": "486bd30d413926c7308a44b7e2cd7c102ebe120f",
          "message": "docs: fix stale SRFI count and incomplete changelog entry\n\nmodule-srfi.md still said '24 supported SRFIs' after SRFI-263 brought\nthe count to 25. The SRFI-263 CHANGELOG entry only described the first\nof the two bugs found/fixed during that work (the diamond-inheritance\ndedup fix); added the second (cyclic-parent-graph segfault, found by\nthe code-review subagent) to match what actually happened and what the\ncommit message for 32b6c8d already says.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-02T06:52:38+10:00",
          "tree_id": "f8b4371055af3471aac7a8813e32d089534f262d",
          "url": "https://github.com/deconstructo/curry/commit/486bd30d413926c7308a44b7e2cd7c102ebe120f"
        },
        "date": 1785617736372,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.557,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 35.983,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.169,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 43.473,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 195.176,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 439.914,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 90.499,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 135.566,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 100.612,
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
          "id": "b4ca93e0221fcd6ab5507be0cc4d6343c5ed091c",
          "message": "docs: expand 12 thin SRFI pages to full per-procedure reference\n\ns8, s18, s59, s98, s113, s125-s126, s128, s132-s133, s145, s158, s194,\ns227 had been extracted near-verbatim from the old monolithic\nmodule-srfi.md, which was itself already terse for these libraries --\na single run-on paragraph naming procedures with no individual\nsignatures, parameter/return-type documentation, or worked examples.\nRewrote each against its actual lib/curry/modules/srfi/sN/*.scm source\n(not just the SRFI text, since several deviate from it in curry-specific\nways worth documenting explicitly -- thread-join!'s silently-swallowed\nexceptions, hash-table-ref's thunk-vs-default-value distinction between\nlibraries, set-xor!'s not-actually-in-place implementation, etc.) and\nverified every code example against the actual built interpreter rather\nthan transcribing from memory.\n\ns1, s27, s215, s170, s112, s238 were already adequate (either written\ncarefully from the start, or -- for s170/s112/s238 -- correctly thin\nsince they're one-line re-exports of C modules documented in full\nelsewhere) and are unchanged.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-02T07:12:32+10:00",
          "tree_id": "2690971ee9811f7beec8389fa968d0ef6c04b2a6",
          "url": "https://github.com/deconstructo/curry/commit/b4ca93e0221fcd6ab5507be0cc4d6343c5ed091c"
        },
        "date": 1785618820127,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.727,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 35.805,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.081,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 42.252,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.272,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 431.452,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 89.658,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 136.398,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 100.519,
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
          "id": "7ee4d9f4ef521b1e886342b40af477194c4b1a47",
          "message": "feat: SRFI-111 (Boxes) and SRFI-195 (Multiple-value boxes)\n\nSingle-value mutable box (box/box?/unbox/set-box!) and its extension to\nzero-or-more values (box-arity/unbox-value/set-box-value!). (srfi s111\nboxes) stores a vector of 0+ values from the start, so (srfi s195\nmultiple-value-boxes) re-exports its box/box?/unbox/set-box! unchanged\nrather than reimplementing them -- trivially satisfying SRFI-195's\nrequirement that bindings shared between the two SRFIs be identical when\nboth are imported. (srfi 111)/(srfi srfi-111) and (srfi 195)/(srfi\nsrfi-195) shims included.\n\nCode review (independent subagent) found that unbox-value/set-box-value!\npassed a caller-supplied index straight into curry's core list-ref, which\nsegfaults on out-of-range input rather than raising, and silently returned\nthe wrong element for a negative index. Fixed by validating the index\nagainst box-arity before it reaches list-ref.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-02T08:05:17+10:00",
          "tree_id": "6ab877856145e1b1da99e698351fde93ee1dff43",
          "url": "https://github.com/deconstructo/curry/commit/7ee4d9f4ef521b1e886342b40af477194c4b1a47"
        },
        "date": 1785621975636,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.967,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 36.133,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.035,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 42.688,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 193.336,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 435.081,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 87.458,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 133.283,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.102,
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
          "id": "5286f81e74696bf3ce789f6c5fc8fef75d013d1d",
          "message": "feat: SRFI-252 (Property Testing), plus SRFI-64 doc/naming gap\n\nProperty-based testing layered on (srfi s64 testing): test-property,\ntest-property-expect-fail, test-property-skip, test-property-error,\ntest-property-error-type, a property-test-runner with failure-input\nreporting, and the SRFI's full fixed generator suite (booleans, chars,\nstrings, symbols, bytevectors, the numeric tower, and list/vector/pair/\nprocedure-generator-of). Built entirely on s64's exported public API\n(%run-assert, test-skip, test-expect-fail) rather than its private\npass/fail-count internals, so bookkeeping is exactly s64's own\nalready-tested logic. exact-complex-generator/exact-integer-complex-\ngenerator raise (curry has no exact complex representation), and\ncomplex-generator aliases the inexact form accordingly, both per the\nSRFI's own allowance for that situation. (srfi 252)/(srfi srfi-252)\nshims included.\n\nAlso closes a documentation gap noticed while writing this: (srfi s64\ntesting) had no doc page, no index.md entry, and no (srfi srfi-64) shim.\nAdded all three.\n\nCode review (independent subagent) found two real bugs, both fixed:\nricher failure detail (failing inputs, underlying error) was attached to\nthe result-alist after on-test-end had already fired and read it, so it\nwas silently dropped -- fixed via a temporary on-test-end wrapper; and\nthe trial loops recursed from inside their own `guard` form rather than\nafter it returned, which isn't tail-call-transparent and segfaulted\naround 200k runs -- fixed by moving recursion outside guard's extent.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-02T21:35:42+10:00",
          "tree_id": "2416dbd29dc46f94e0b7469a52b79dff33048cf2",
          "url": "https://github.com/deconstructo/curry/commit/5286f81e74696bf3ce789f6c5fc8fef75d013d1d"
        },
        "date": 1785670587560,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.351,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.46,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.142,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 40.492,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.048,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 407.076,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 85.097,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 131.283,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 105.242,
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
          "id": "dfe7f28cdbcd9c1ba2bee0793953d38b125bc7d9",
          "message": "feat: SRFI-209 (Enums and Enum Sets)\n\nTyped, ordered symbolic constants (name/ordinal/value) grouped into\ndisjoint enum types, plus enum sets with the usual set algebra\n(union/intersection/difference/xor, each with a functional and a\nlinear-update ! form), make-enum-comparator (an SRFI-128 comparator\nordering by ordinal), the full R6RS-compatibility layer\n(make-enumeration, enum-set-universe, etc.), and the\ndefine-enum/define-enumeration macro sugar. Enum sets are a boolean\nmembership vector indexed by ordinal, and enum types use a two-phase\nbuild (type record created first with a placeholder enums-vec, patched\nin once the enum records that reference it back exist). (srfi\n209)/(srfi srfi-209) shims included.\n\ndefine-enum/define-enumeration's type-name and constructor-name macros\nboth need to resolve to the same underlying enum-type object, but\ncurry's define-syntax doesn't hygienically rename identifiers a macro\ntemplate introduces -- solved with a private registry rather than a\nshared hidden temporary.\n\nCode review (independent subagent) found the registry was initially\nkeyed on the type name alone, which collides across two independently\nauthored libraries that happen to choose the same type-name for\nunrelated define-enum types -- and unlike an ordinary name collision, an\nR7RS renaming import doesn't rescue it. Narrowed (not fully closable\nwithout gensym/datum->syntax, which curry's syntax-rules doesn't have)\nby folding the constructor-name into the key too, verified against the\nexact renamed-import scenario that exposed it, and documented as a\nresidual limitation.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-02T21:55:10+10:00",
          "tree_id": "f1a115d6a81056a731c67f1f3196dbdbb81166e7",
          "url": "https://github.com/deconstructo/curry/commit/dfe7f28cdbcd9c1ba2bee0793953d38b125bc7d9"
        },
        "date": 1785671774818,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 22.458,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.16,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.034,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.194,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.071,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 371.302,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.992,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.707,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.022,
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
          "id": "4dc3e4d7e5b201c8b9fdf223a2aa3e18fe0fd1d5",
          "message": "fix: let-values/let*-values missing from bytecode compiler\n\nsrc/compiler.c had zero handling for S_LET_VALUES/S_LET_STAR_VALUES, so\nboth R7RS special forms only worked in the tree-walker (eval.c) -- any\nscript run through the normal compiled path (curry script.scm, curry -e,\nor a cached .scc) hit an unbound-special-form failure using either.\n\nFixed by desugaring both to nested call-with-values/lambda forms at\ncompile time. let-values uses fresh temp names in each producer's\nconsumer lambda so a later producer can never observe an earlier\nbinding (correct parallel semantics); let*-values nests directly with\nthe real formal names (correct sequential semantics), mirroring\ncompile_let_star's existing recursive self-embedding style.\n\nIndependent code review found no correctness bugs in the desugaring\nitself, but surfaced two pre-existing, out-of-scope issues while\ntesting: call-with-values' own primitive doesn't preserve proper tail\ncalls for its consumer (shared with receive, which has the identical\ngap), and call-with-values with a zero-value producer fails outright.\nBoth documented in code comments and filed as separate backlog items\nrather than fixed here.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-03T01:21:45+10:00",
          "tree_id": "6687f7fa9171c7a31266f8c725e7086d078460f2",
          "url": "https://github.com/deconstructo/curry/commit/4dc3e4d7e5b201c8b9fdf223a2aa3e18fe0fd1d5"
        },
        "date": 1785684149574,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.542,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.162,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.138,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 40.142,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 206.295,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 428.229,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 86.209,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 136.574,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.086,
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
          "id": "acea8b8cf6124a6a44bdc1e960ce2915d0b10e66",
          "message": "feat: SRFI-210 (Multiple Values)\n\nConvenience syntax/procedures for multiple-value programming, layered\nentirely on call-with-values: apply/mv, call/mv, list/mv, vector/mv,\nbox/mv, value/mv, coarity, set!-values, with-values, case-receive,\nbind/mv, plus the chaining/composition helpers compose-left/\ncompose-right/map-values/bind-list/bind-box/bind and small utilities\nlist-values/vector-values/box-values/value/identity. (srfi 210)/(srfi\nsrfi-210) shims included.\n\ncall/mv's first implementation attempt was a recursive macro building\nnested consumer lambdas, one per producer, each rebinding the same\nparameter name (vals) -- wrong, caught by a test with three producers:\ncurry's macro expansion doesn't hygienically rename a\ntemplate-introduced binding per recursive expansion (the same gap\nSRFI-209's registry design found), so reusing that literal name at\nevery nesting level meant each deeper lambda silently shadowed the\nouter one, and every reference ultimately resolved to the last\nproducer's values only. Fixed by not introducing any named binding per\nproducer at all -- each producer becomes a zero-argument thunk, and the\nactual value collection happens in ordinary runtime code over a plain\nlist of thunks.\n\nIndependent code review audited every other macro in the file for the\nsame shadowing risk (bind/mv, case-receive, set!-values) and found none\nrecur; added a handful of suggested coverage-gap tests (3+ item cases,\nnegative-index bounds checks, multi-transducer chains).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-03T01:36:25+10:00",
          "tree_id": "e9a75cc492351559f2366f876dc5b8c3b49c54be",
          "url": "https://github.com/deconstructo/curry/commit/acea8b8cf6124a6a44bdc1e960ce2915d0b10e66"
        },
        "date": 1785685045686,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.896,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 32.943,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.973,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.318,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 190.522,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 372.648,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.522,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.863,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 100.25,
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
          "id": "7fb42d85d403bccb0e7831a97f170c9f15ec0a0e",
          "message": "feat: SRFI-54 (Formatting)\n\ncat, an order-independent object-to-string formatter: every optional\nargument is recognized by its own shape (a symbol, a list starting with\na procedure, a pair of two procedures, and so on), not by position.\nCovers every non-number argument fully (writer/pipe/take/width/char/\nport/string/converter) and every number argument for real numbers in\ndecimal radix fully (exactness/radix/sign/precision/separator); a\nnon-decimal radix is supported for exact integers only. Precision\nrounds using a flonum's own printed decimal digits rather than its raw\nbinary value, matching the spec's own worked example (129.985 rounds\nto 129.98, not 129.99, since the true double value sits a hair above\nthe halfway point but the digits as written don't). (srfi 54)/(srfi\nsrfi-54) shims included.\n\nDocumented scope limitations rather than guessed at: non-decimal radix\ncombined with an inexact number, precision applied to a complex number,\nand a separate pre-existing gap in curry's own number->string (doesn't\nalways round-trip a flonum losslessly).\n\nIndependent code review found and fixed two bugs: precision of exactly\n0 dropped the decimal point entirely, breaking one of the spec's own\nworked examples that relies on the point being there to take off; and a\nmalformed take-spec with a non-integer second element was accepted by\nthe argument classifier and only failed later with a confusing\nlow-level type error instead of a clean classification error.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-03T01:55:02+10:00",
          "tree_id": "94cd9a65d72012274f9bc200cff7998ffbd67e2d",
          "url": "https://github.com/deconstructo/curry/commit/7fb42d85d403bccb0e7831a97f170c9f15ec0a0e"
        },
        "date": 1785686160098,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.486,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.656,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.136,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.095,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 206.476,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 362.035,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.098,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 127.121,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 107.864,
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
          "id": "1192ddc634d35b039c5ba08207a95579f9e929a6",
          "message": "fix: call-with-values with a zero-value producer\n\n(call-with-values (lambda () (values)) (lambda () ...)) failed with\n\"too many arguments (got 1, need 0)\". vm.c's OP_VALUES bytecode (what\nthe compiler emits for a literal (values ...) call) special-cased zero\narguments by pushing V_VOID instead of building a genuine zero-count\nValues object -- unlike eval.c's tree-walker equivalent, which already\nbuilt a real empty Values object for this case. Since\ncall-with-values's consumer decides how many arguments to apply by\nchecking whether the producer's result is a Values object, V_VOID\n(indistinguishable from any other void-returning single value) was\napplied as one argument instead of zero.\n\nFixed by letting the n == 0 case fall through to the same path n >= 2\nalready used. Also unblocks coarity/bind/mv/etc. from SRFI-210 being\nused with a zero-value producer, previously a documented known\nlimitation there.\n\nIndependent code review confirmed the fix's allocation path is sound\nfor n=0, cross-checked every other Values/vis_values consumer in the\ncodebase (tree-walker, GC tracers, prim_values/prim_call_with_values)\nfor the same class of bug, and found none.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-03T02:08:46+10:00",
          "tree_id": "9bf58ec9fd8725e8f2e13ae5c5eeed7ef11355a5",
          "url": "https://github.com/deconstructo/curry/commit/1192ddc634d35b039c5ba08207a95579f9e929a6"
        },
        "date": 1785686982910,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.715,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 32.935,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.516,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.302,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.19,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 374.197,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.789,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 124.385,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 104.08,
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
          "id": "b3f5f0c0b3c3a3a156fa1c5a0afb2f3c6e2e6444",
          "message": "fix: call-with-values/receive/let-values/let*-values now get proper TCO\n\nprim_call_with_values (builtins.c) invokes its consumer via a real\nnested C call, which is fine for a one-shot call but meant any of\nthese forms sitting in the tail position of a self-recursive loop\naccumulated one such nested call PER ITERATION, hitting curry's\ncall-stack limit instead of looping forever.\n\nFixed with a new OP_TAIL_CALL_WITH_VALUES bytecode op, emitted only\nwhen the compiler sees a literal 2-argument (call-with-values producer\nconsumer) -- the same unconditional syntactic special-casing\nconvention already used for apply/values -- in tail position. It\nreuses the current call frame for a BcClosure consumer exactly the way\nplain OP_TAIL_CALL does, instead of going through\nprim_call_with_values at all. Non-tail usage (as an ordinary\nsubexpression) is unaffected, still going through the existing\nprimitive.\n\nSince OP_TAIL_CALL_WITH_VALUES was inserted into the middle of the\nopcode enum, every opcode after it shifted its numeric value -- bumped\n.scc cache format version (v4 -> v5) so any cache compiled by an older\nbinary is rejected and recompiled rather than silently misinterpreted\nunder the new numbering. Building this fix initially surfaced as a\nwave of unrelated-looking test failures across the suite, all traced\nto exactly this: stale .scc caches from before the opcode reordering.\nUpdated one test_cli.sh assertion that hardcoded the format-version\nbyte to match.\n\nIndependent code review confirmed stack arithmetic, GC safety, and the\ncompiler dispatch's fast-path gating are all correct; found one test\nthat didn't actually exercise what its name claimed (a non-tail\nargument expression, not genuine tail position) and it's fixed here.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-03T02:25:56+10:00",
          "tree_id": "abbab58089c6a6dddd685ccd6af06dbc9e2af8ee",
          "url": "https://github.com/deconstructo/curry/commit/b3f5f0c0b3c3a3a156fa1c5a0afb2f3c6e2e6444"
        },
        "date": 1785688007169,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 26.421,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.459,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 8.623,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 36.6,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 207.27,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 370.241,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.666,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.209,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 103.395,
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
          "id": "ed791eeb43e2cf2300b271f8c7d642fe9268ec7b",
          "message": "fix: flonum number->string/display/write no longer lose precision\n\nnum_to_string's flonum branch (numeric.c), scm_write's own direct\nflonum case (port.c), and the matrix/tensor/multivector/f64vector\nelement printers all used a bare \"%g\" -- 6 significant digits by\ndefault, nowhere near enough for a double, which can need up to 17.\n(display 3.14159265358979) printed \"3.14159\"; (number->string\n129.985001) printed \"129.985\", which read back as a different number\nentirely.\n\nFixed with a shared helper (num_flonum_to_shortest_cstr) that finds\nthe shortest decimal string that round-trips back to the exact same\ndouble via strtod, with a safety check before trying to avoid\nneedless scientific notation for \"round\" values (100.0 -> \"100\", not\n\"1e+02\") that verifies the extra digits revealed really are trailing\nzeros and not genuine precision noise from a value that isn't exactly\ndecimal-clean in binary (1e300 stays the clean \"1e+300\", not 17\ndigits of binary-conversion noise).\n\nIndependent code review stress-tested round-trip correctness across\nsubnormals, DBL_MIN/MAX, power-of-10 boundaries, and everyday\ndecimals, verified buffer sizes at all five call sites, and confirmed\nno bugs; added a couple of suggested negative-number regression tests.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-03T02:53:21+10:00",
          "tree_id": "f69ea30be3ffe08de07fe36c7841bf768b8c840a",
          "url": "https://github.com/deconstructo/curry/commit/ed791eeb43e2cf2300b271f8c7d642fe9268ec7b"
        },
        "date": 1785689654821,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.489,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 27.887,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.173,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 32.567,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 206.513,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 375.604,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.564,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 125.739,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 104.783,
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
          "id": "5e3538903d2cef5657d9fa3a375672526cee36a0",
          "message": "Update Formula/curry.rb sha256 for v1.14.2",
          "timestamp": "2026-08-03T02:57:14+10:00",
          "tree_id": "b4cede1b6c861ed4204124972bbdae41621f0ceb",
          "url": "https://github.com/deconstructo/curry/commit/5e3538903d2cef5657d9fa3a375672526cee36a0"
        },
        "date": 1785689887092,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.796,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.116,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.944,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.251,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.601,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 391.145,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 75.592,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 122.469,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 100.746,
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
          "id": "ffcf3b28c57cd8bbfd4c2031068d25bd494af411",
          "message": "docs: file the v1.14.2 changelog entries under a versioned heading",
          "timestamp": "2026-08-03T02:58:45+10:00",
          "tree_id": "b86716db1a619057e3f3fa4f353463ba045efa3d",
          "url": "https://github.com/deconstructo/curry/commit/ffcf3b28c57cd8bbfd4c2031068d25bd494af411"
        },
        "date": 1785689967009,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.442,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 27.791,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.192,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 32.71,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.701,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 426.429,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 85.197,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 134.026,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 105.399,
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
          "id": "c55a980a11a96dd70155e750029ebd1b8c6e5e6a",
          "message": "chore(formula): update sha256 for v1.15.0 tarball\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-04T19:36:10+10:00",
          "tree_id": "7f30388b0e117417ae0e102f2be8c363ac8f60e4",
          "url": "https://github.com/deconstructo/curry/commit/c55a980a11a96dd70155e750029ebd1b8c6e5e6a"
        },
        "date": 1785836223377,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.525,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 27.295,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.132,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 32.308,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 209.198,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 394.733,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 84.283,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 131.9,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 103.653,
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
          "id": "277cc49d627e5f987175abc0dc4409ebf5e2e662",
          "message": "docs: sync roadmap.md from v1.8.0 to v1.15.0\n\nThe roadmap was last corrected against actual shipped state at\nv1.8.0 (2026-07-17); seven point releases and one minor version\nshipped since without it being updated.\n\nFlips three \"Where we are now\" table rows from unstarted to shipped:\nSlim CLOS Layer 1 (v1.11.0, `(curry oop)`), property-based testing\n(v1.14.2, SRFI-252 -- differently-shaped than the bespoke\ndefine-property/check-property DSL Phase 9 sketched), and scientific\nI/O (HDF5/NetCDF/FITS, shipped undated -- differently-shaped than the\n(curry io ...) namespace Phase 9 sketched, missing only the native\n.curry-tensor serialisation format). Phase 7 and Phase 9's own\nsections get the same done/shipped-differently/not-started split\nPhase 6 already used for its own partial completion. Adds the missing\nv1.8.3 through v1.15.0 rows to the summary timeline table and drops\nPhase 9 from the remaining-phases table since it's done. Fixes a\nstale docs/pkg-design.md path (actually docs/guides/pkg-design.md) in\nthree places.",
          "timestamp": "2026-08-04T19:44:42+10:00",
          "tree_id": "0ccac30e3b5d8c394762f8304dc74e1a6a7d01be",
          "url": "https://github.com/deconstructo/curry/commit/277cc49d627e5f987175abc0dc4409ebf5e2e662"
        },
        "date": 1785836827331,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.427,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.689,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.93,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.944,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 189.865,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 359.656,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 69.459,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.022,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.15,
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
          "id": "a6c0a548e09efcabd9709d08fbdecc8b0f32dc1e",
          "message": "chore(formula): update sha256 for v1.16.0 tarball",
          "timestamp": "2026-08-05T18:27:55+10:00",
          "tree_id": "166bc0c0dcfb205046eacfdee5b5bc8390106f52",
          "url": "https://github.com/deconstructo/curry/commit/a6c0a548e09efcabd9709d08fbdecc8b0f32dc1e"
        },
        "date": 1785918538585,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.605,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.182,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.124,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.773,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 209.96,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 341.217,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 69.868,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 123.776,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 103.515,
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
          "id": "783a6d763af1b4ce021f731f4a5b3ffcd366c645",
          "message": "Release v1.17.0",
          "timestamp": "2026-08-06T20:50:49+10:00",
          "tree_id": "3a34c7c37a52e90009e651764aabcedd90c2ad47",
          "url": "https://github.com/deconstructo/curry/commit/783a6d763af1b4ce021f731f4a5b3ffcd366c645"
        },
        "date": 1786013499143,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.702,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 27.812,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.116,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.034,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.293,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 370.389,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 69.422,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 119.893,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.121,
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
          "id": "c26491003eafee38e881081e4160b4c04712e965",
          "message": "chore(formula): update sha256 for v1.17.0 tarball",
          "timestamp": "2026-08-06T20:51:43+10:00",
          "tree_id": "c956d2bbe292ea443054ee86e753626a7d983518",
          "url": "https://github.com/deconstructo/curry/commit/c26491003eafee38e881081e4160b4c04712e965"
        },
        "date": 1786013545735,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 25.862,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.78,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 7.286,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 35.585,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 239.361,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 397.312,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 83.318,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 141.455,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 119.035,
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
          "id": "f53ef52e9b3a547c5efc86dad615a0b9c0b86ac4",
          "message": "chore(formula): update sha256 for v1.17.1 tarball",
          "timestamp": "2026-08-07T17:19:40+10:00",
          "tree_id": "ac29ac8cd6b729e1033241a796f474fa050dfa1b",
          "url": "https://github.com/deconstructo/curry/commit/f53ef52e9b3a547c5efc86dad615a0b9c0b86ac4"
        },
        "date": 1786087235938,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.829,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 27.657,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.932,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.036,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.916,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 360.668,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 69.206,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.405,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.083,
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
          "id": "b1b4e80a34f7d34797e9630f808bf39efa9f7705",
          "message": "chore(formula): update sha256 for v1.17.2 tarball",
          "timestamp": "2026-08-07T21:25:36+10:00",
          "tree_id": "58fe059c1e7f12186f144844c457f2c9dc340360",
          "url": "https://github.com/deconstructo/curry/commit/b1b4e80a34f7d34797e9630f808bf39efa9f7705"
        },
        "date": 1786101984117,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 22.317,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 27.869,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.259,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.096,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 212.909,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 403.421,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 85.272,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 134.062,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 105.293,
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
          "id": "3e4c9e5b8bc6c1daef78a9bb72f67601a02d11a4",
          "message": "docs(pkg-design): add public package-manager design doc, supersede prior survey\n\ndocs/thoughts/package-management-design.md inherits every settled\nconclusion from docs/guides/pkg-design.md except native-capability\nhandling, which it deliberately reverses: FFI-first (runtime dlopen,\nfollowing the working (curry hdf5) pattern) as the default, with\nCMake source-compilation kept as an explicit fallback tier for what\n(curry ffi) structurally can't reach (no struct-by-value, callbacks,\nor variadics).\n\nAlso adds: bundled tests/docs in the manifest, a develop/patch\nworkflow for local package development, targeted single-edge lockfile\nupdates, version retraction in the index schema, optional/weak\ndependencies, a registry-PR-submission helper, and a porting path for\nCHICKEN eggs and SRFIs. Consolidates what must change in curry itself,\nwith each item explicitly resolved (in v1 scope or deferred) rather\nthan left open.\n\nUses Akkadian terminology (bīt ṭuppi / iškāru / iškāru aḥûtu)\nconceptually throughout, kept deliberately out of all concrete syntax —\nmanifest extension, CLI verbs, and directories stay plain English.\n\npkg-design.md is marked superseded (kept as historical background, not\ndeleted); roadmap.md's three references updated to point at the new\ndocument.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-08T11:03:24+10:00",
          "tree_id": "dacf57e3122bb648f12592bd119bd2eb3e5338b6",
          "url": "https://github.com/deconstructo/curry/commit/3e4c9e5b8bc6c1daef78a9bb72f67601a02d11a4"
        },
        "date": 1786151314837,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.977,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 24.128,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.322,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.2,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 152.371,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 298.202,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.208,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 96.575,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 80.236,
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
          "id": "b6e21df4c54189bd632067e343b2070237eb3bad",
          "message": "chore(formula): update sha256 for v1.17.7 tarball\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-09T03:30:02+10:00",
          "tree_id": "c2a3e2c880eeb1269debb28e703cc560c5a2ca1d",
          "url": "https://github.com/deconstructo/curry/commit/b6e21df4c54189bd632067e343b2070237eb3bad"
        },
        "date": 1786210356397,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.573,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 24.404,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.742,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 28.976,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 162.692,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 268.804,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 55.315,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 94.937,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 80.388,
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
          "id": "676bfcad888a9c5247a15963534706387f3795c4",
          "message": "feat(zeromq): add (curry zeromq), ZeroMQ messaging sockets\n\nContext/socket lifecycle, zmq-bind!/zmq-connect!, zmq-send!/zmq-recv\n(blocking and non-blocking via 'dontwait), the common socket options\n(SUBSCRIBE/UNSUBSCRIBE/LINGER/RCVTIMEO/SNDTIMEO/IDENTITY), and\nmultipart messages ('sndmore/zmq-more?), via libzmq dlopen'd lazily at\nruntime -- the same pattern (curry hdf5)/(curry ncurses)/(curry\ngraphviz) already use. Messages are plain bytevectors at the primitive\nlevel; zmq-send-string!/zmq-recv-string are thin string->utf8/\nutf8->string convenience wrappers. zmq_poll (socket multiplexing,\nneeding struct-array FFI marshaling) and the CURVE/PLAIN security\nmechanisms are out of scope for this pass.\n\nIndependent review found two real bugs before this shipped, both in\nzmq-recv: a blocking call (no 'dontwait) on a socket with RCVTIMEO set\nraised once the deadline elapsed instead of returning #f -- EAGAIN is\nthe exact same errno for that case and for a 'dontwait call finding\nnothing queued, and the old check only special-cased the latter,\ndefeating the normal \"block with a give-up deadline\" idiom RCVTIMEO\nexists for. Separately, zmq_msg_close was skipped on any recv failure\nthat wasn't EAGAIN (e.g. calling recv on a push/pub socket, which\nlibzmq itself rejects) -- not an observed leak, but a violation of the\nzmq_msg_init/zmq_msg_close pairing contract. Both fixed: EAGAIN is now\ndetected by matching zmq_strerror's text (no portable numeric errno\nacross platforms) regardless of which flag the caller passed, and\nzmq_msg_close runs on every path out of zmq-recv, success or failure.\n\n17 new assertions in tests/zeromq_tests.scm, all exercised against\nreal inproc:// socket pairs (PUSH/PULL, REQ/REP, PUB/SUB with a\nsubscription filter, multipart); full 85/85 ctest suite passes; see\ndocs/reference/module-zeromq.md.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-09T19:59:24+10:00",
          "tree_id": "aa2bc586dae28b84e056496db73770d04a86f2c8",
          "url": "https://github.com/deconstructo/curry/commit/676bfcad888a9c5247a15963534706387f3795c4"
        },
        "date": 1786270288306,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.559,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.599,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.105,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.958,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 197.64,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 377.772,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.387,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.697,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.655,
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
          "id": "b6b2d0adc83ec1424176c7add61dfdf89a0791a7",
          "message": "chore(formula): update sha256 for v1.17.11 tarball\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-09T20:11:28+10:00",
          "tree_id": "f27579207be259e3a449e5ed88abd8a130528fd8",
          "url": "https://github.com/deconstructo/curry/commit/b6b2d0adc83ec1424176c7add61dfdf89a0791a7"
        },
        "date": 1786270340246,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.914,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.462,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.95,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.839,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.752,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 373.863,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.801,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.422,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.889,
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
          "id": "0bb387438d1b893de283687c6dd037063eddf6e2",
          "message": "chore(formula): update sha256 for v1.18.0 tarball",
          "timestamp": "2026-08-10T23:38:32+10:00",
          "tree_id": "050ba965156295db8791580bab03e8d55b63c82a",
          "url": "https://github.com/deconstructo/curry/commit/0bb387438d1b893de283687c6dd037063eddf6e2"
        },
        "date": 1786369173584,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.009,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.88,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.962,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.87,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.444,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 382.339,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.96,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 123.257,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.414,
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
          "id": "d4689a287597f349b51ffd3e9afb783d220fec7a",
          "message": "chore(formula): update sha256 for v1.19.0 tarball",
          "timestamp": "2026-08-11T12:01:13+10:00",
          "tree_id": "9a5641e20bb2dbed5acd1ce65a3a6d8b45380f2c",
          "url": "https://github.com/deconstructo/curry/commit/d4689a287597f349b51ffd3e9afb783d220fec7a"
        },
        "date": 1786413740939,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.751,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.728,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.809,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.972,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 162.4,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 266.469,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 55.62,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 95.84,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 83.054,
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
          "id": "a7768913b2d3d7edeb30f5f549cea752936ddb48",
          "message": "docs: add CONTRIBUTING.md, issue templates; fix unresolved merge in LICENSE\n\nLICENSE had unresolved git conflict markers committed straight into the\nfile since its original addition -- both sides were GPL-3.0 text (one a\nshort stub pointing at the full text, one the actual full text), so the\nfix keeps the full official GPL-3.0 body verbatim with the project's\ncopyright notice in the standard \"how to apply\" slot at the top, not a\nrewritten or paraphrased license.\n\nCONTRIBUTING.md covers build/test/style/review-focus/commit-message\nconventions drawn from CLAUDE.md and observed git history, for anyone\nwho wants to contribute without reading the whole AI-workflow doc first.\nIssue templates (bug report, feature request, a discussions link for\nopen questions) give a starting structure instead of a blank textarea.",
          "timestamp": "2026-08-11T18:15:46+10:00",
          "tree_id": "de61ab19241e19911d82ddd1f3ad020efbdd6f12",
          "url": "https://github.com/deconstructo/curry/commit/a7768913b2d3d7edeb30f5f549cea752936ddb48"
        },
        "date": 1786436206590,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.585,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 24.455,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.761,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 28.778,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 159.162,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 264.707,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 54.291,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 95.385,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 79.993,
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
          "id": "9d50df584d07210c28879959ec7ec573188994d0",
          "message": "fix(tts): address independent code review findings\n\nTwo real, if minor, robustness gaps found by a fresh code-review pass:\n\n- macos.scm's %trim only stripped ASCII space, not general whitespace,\n  unlike the sibling espeak.scm tokenizer which already uses\n  char-whitespace?. `say -v ?`'s column padding has only ever been\n  observed as plain spaces, but nothing guarantees that holds across\n  every locale/macOS build -- a stray tab would otherwise silently end\n  up inside an extracted voice name, breaking #:voice lookups.\n\n- #:rate had no validation at all, unlike #:voice (which is checked\n  against the backend's own live voice list). A rational, negative, or\n  non-numeric #:rate either produced a value neither `say -r` nor\n  `espeak-ng -s` could parse, or an unhelpful raw type error, instead\n  of a clean 'tts-error. Added %validate-rate (positive exact integer\n  only), matching #:voice's own discipline.\n\nAlso documented two things the review flagged as correct-but-easy-to-\nmiss: tts-stop doesn't itself reap the process (pair it with tts-wait,\nsame as (curry posix)'s own process-kill/process-wait contract, or a\nzombie persists for the life of the curry process), and #:voice usage\nmeans two subprocess spawns per tts-speak/tts-save call, not one (one\nto validate against a live tts-voices listing, one to actually\nspeak/render).\n\nEverything else the review checked came back clean: argv construction\n(no shell, no injection surface, flags/values always kept as separate\nlist elements), voice-list parsing bounds-checking, process reaping\nvia process-run's own C-level waitpid, and the #:foo keyword scanner's\nbehavior against (curry posix)'s own find_kwarg.\n\nFull ctest suite (89 tests) passes.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-11T19:05:50+10:00",
          "tree_id": "78c22d9c9a5829d0780278ea1016b8c35a0496d7",
          "url": "https://github.com/deconstructo/curry/commit/9d50df584d07210c28879959ec7ec573188994d0"
        },
        "date": 1786439300385,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 22.133,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.539,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.278,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.655,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 204.811,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 343.234,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.15,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.58,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.28,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "15fec7a314bdced02596776c875dd09f2f1d95b8",
          "message": "TTS config, cond-expand, SRFI-279/26, load-path fixes, syntax-rules partial hygiene (#22)\n\n* feat(tts): add current-tts-voice/rate/language parameters\n\nLets tts-speak/tts-save/tts-speak-async be called with just text once\nthe backend, voice/rate, and language defaults are set up front,\nmirroring the existing current-tts-backend pattern. current-tts-language\nresolves to a concrete voice by locale prefix against the active\nbackend's own tts-voices listing rather than being passed to the\nbackend directly (say/espeak-ng only take voice names, not locales).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat: implement R7RS cond-expand and (features)\n\ncond-expand was a pre-interned symbol (S_COND_EXPAND) that was never\nactually dispatched anywhere -- a real R7RS 4.2.9 gap. Adds it as a\ngenuine special form, resolved via a shared features.c/.h module (the\nrequirement grammar: feature identifiers, and/or/not, (library <name>),\nelse) at three call sites: eval.c (tree-walker, expression position,\nresolved at runtime), compiler.c (VM path, resolved once at compile\ntime -- matches R7RS's expansion-time semantics but means a no-match\nerror surfaces during compilation of the enclosing form rather than its\nexecution), and modules.c (define-library declaration position, where a\nmatched clause's body is further declarations like (import ...), not\nexpressions -- the actual motivating use case: SRFI-279's own reference\nimplementation structures its 279.sld exactly this way to dispatch\nchibi/kawa/guile/else).\n\n(library <name>) is backed by a new modules_available(), a non-raising\nprobe refactored out of modules_load()'s existing search/load logic.\n(features) is a new R7RS 6.13.3 builtin sharing the same feature list.\n\nAlso fixes two unchecked-vcar crashes surfaced by code/security review\non malformed input: a non-pair cond-expand clause, and a non-list\ndefine-library declaration -- both now raise a clean Scheme-level error\ninstead of segfaulting.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat: implement SRFI-279 (In(tro)spection Protocol)\n\nCurry-native implementation of inspect-properties/inspect-describe,\nwritten directly against curry's own primitives (srfi 1/69/111/113)\nrather than porting the SRFI's reference generic.scm, which depends on\nsrfi 14/26/160/253 -- none of which curry has.\n\nCovers: object (write/display), number, boolean, pair, symbol,\ncharacter, string, vector, bytevector, error-object, hash-table (srfi\n69), box (srfi 111), set/bag (srfi 113), and record/record-type.\nDeliberately deferred (documented in docs/reference/srfi/s279.md):\nprocedure properties (no arity/name introspection in curry yet),\nnumeric-vector properties (no srfi 4/160), char-set properties (no\nsrfi 14), library/environment properties (no Scheme-level module\nregistry enumeration).\n\nAdds four new primitives record?/record-type?/record-rtd/\nrecord-type-name/record-type-field-names ((rnrs records inspection)\nnaming) -- curry had no way to ask \"is this any record\" or inspect an\nRTD generically before, only the type-specific predicate/accessor\nclosures define-record-type hands out.\n\nTwo independent review rounds (code + security) verified: correct\ntype-dispatch ordering (srfi 113 bags are hash tables under the hood,\nso bag? must be checked before the generic hash-table? branch -- fixed\nduring testing, not by review), argument-type safety on the new\nprimitives, no security-relevant encapsulation bypass (traced every\ndefine-record-type in the tree; no credential/key material is ever\nrepresented as a Scheme record). Fixed one doc/code mismatch: the\nutf8->string bytevector-properties entry was documented as conditional\non valid UTF-8, but curry's utf8->string never validates -- now\ndocumented accurately as always-present with possibly-garbled content.\n\nManual testing (documented as known limitations in s279.md, out of\nscope to fix here) found two pre-existing core bugs: curry's write/\ndisplay have no cycle detection at all (inspect-properties hangs on\nany circular object, since it always write/displays the object first),\nand (string->number \"\") returns 0 instead of #f -- guarded explicitly\nso it doesn't leak into every empty string's properties.\n\n85 new tests (tests/srfi_s279_inspect_tests.scm), all 86 ctest suites\npassing.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix: resolve (load ...)/(include ...) relative paths against the loading file's directory, not cwd\n\nFound while porting SRFI-279 upstream: a library whose own directory\nwasn't the process's cwd could never portably (include \"sibling.scm\")\n-- every relative load resolved against cwd unconditionally, so the\nper-implementation file layout SRFI-279's own reference repo uses\n(279.sld's (include \"chibi.scm\") etc., each living alongside its .sld)\nwould silently break for curry unless the process happened to be run\nfrom that exact directory.\n\nAdds a directory-context stack (load_dir_mark/load_dir_release/\nload_push_dir, runtime.c) that scm_load resolves non-absolute paths\nagainst, pushing/releasing around its own read/eval loop. Wired into\nevery path that reads Scheme source from a file: scm_load itself\n((include ...) in eval.c's tree-walker, the load builtin, -l FILE),\nload_scheme_module (modules.c, a second independent file-reading loop\nfor .sld/.scm module files -- what (include ...) inside a\ndefine-library declaration is actually reached from), and main.c's\npositional-script-argument path (so a top-level script's own relative\nloads work too).\n\nThree rounds of review (code, then two security passes) found and\nfixed real bugs along the way, each surfaced by actually running\nreproductions rather than just reading the diff:\n- Exception mid-load left stale stack entries that corrupted a later,\n  unrelated load once the error was caught and execution continued --\n  fixed by switching from push/pop pairing to mark/release (a saved\n  depth snapshot, released back to unconditionally), wrapped in\n  SCM_PROTECT for exception-safe release-then-reraise.\n- The 64-entry depth cap could desync a push/pop pairing; mark/release\n  is self-correcting for this by construction.\n- The directory stack being a plain (non-thread-local) static was a\n  genuine cross-thread data race -- curry's actors each run in a\n  detached POSIX thread and can call load concurrently. Verified via\n  ThreadSanitizer: one actor's free() on a stack slot could race\n  another actor's concurrent read of it, a real use-after-free, not\n  just a lost update. Fixed by making the stack _Thread_local, matching\n  current_handler/current_wind/g_jit_call_depth's existing pattern.\n- _Thread_local then meant a freshly-spawned actor started with an\n  *empty* stack instead of inheriting its spawning thread's directory\n  context, so an actor's own relative loads silently fell back to cwd --\n  found empirically (a test passed only when cwd happened to match the\n  script's own directory, failed 100% of the time once it didn't, e.g.\n  under ctest). Fixed with load_dir_snapshot/load_dir_adopt_snapshot:\n  actor_spawn snapshots the spawning thread's stack (independent\n  strdup'd copies) into the new actor's start struct; actor_thread\n  adopts it before running the actor's closure, and releases it before\n  the thread exits (the adoption was leaking one allocation per actor\n  until this fix). pthread_create failure also frees the snapshot\n  rather than leaking it.\n\n19 new regression tests across tests/include_relative_tests.scm and\ntests/fixtures/load_dir_context/{script_relative_load,\nexception_safety,thread_safety}/, all 90 ctest suites passing.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat: partial hygiene for syntax-rules; add SRFI-26 (cut/cute)\n\ncurry's syntax-rules was fully unhygienic -- a template-introduced\nidentifier (not a pattern variable) was always emitted completely\nunchanged, resolved at the macro's use site. This broke porting\nSRFI-26's standard cut/cute reference implementation, a recursive\nmacro that accumulates a fresh identifier into a growing formals list\nacross several separate expansions of itself: every recursive step's\nintroduced identifier collapsed into the same literal symbol, so\nmultiple <> slots in one cut call all ended up bound to whichever\nargument was passed last.\n\nAdds \"partial hygiene\": after a pattern match, every template-\nintroduced symbol that isn't already a real reference gets one fresh\ngensym for that expansion, substituted consistently throughout its\noutput. \"Already a real reference\" means: a core special-form keyword\n(built from symbol_list.h's own X-macro list), something bound in the\nmacro's own DEFINING environment (captured once at macro-definition\ntime via a new thread-local eval.c's T_SYNTAX dispatch sets around\neach transformer apply(), not the use site -- library bodies run in\nisolated environments, not GLOBAL_ENV), a compile-time-local macro\nname (let-syntax/letrec-syntax/locally-scoped define-syntax -- a\nsecond thread-local, since those live only in compiler.c's own\nSyntaxLocal table), a symbol inside a literal (quote X) subform, or\none of the macro's own declared literals. Does not attempt full\nhygiene in the other direction (a macro's free reference still\nresolves at the use site, so anaphoric-style capture is still\npossible for names that aren't otherwise bound) -- see\nsyntax_rules.c's \"Partial hygiene\" header comment for the full\nrationale, including three heuristics tried and rejected along the\nway, each after breaking a real case:\n  1. Recognizing lambda/let/letrec/do binding-form shapes directly --\n     doesn't catch cut/cute itself, since the introduced identifier\n     isn't textually inside a binding form at the point it's\n     introduced, only several recursive expansions later.\n  2. \"Never used as an operator\" -- false positive on cut/cute's own\n     accumulator shape ((x nse) . nse-bindings), which looks like a\n     call even though it's just a 2-tuple data entry.\n  3. \"Already bound in GLOBAL_ENV\" alone -- broke this codebase's own\n     (curry private lang-aliases) helper macro (%lang-alias-row\n     recursing into itself), since define-library bodies run in an\n     isolated environment, not GLOBAL_ENV. Fixed by threading the\n     macro's actual defining environment through instead.\n\nTwo independent review rounds (code, then security) each found and\ngot fixed real bugs empirically, not just by inspection:\n  - let-syntax/letrec-syntax self-recursion broke in compiled code\n    (any top-level script or lambda body) even after the def_env fix,\n    since those macro names live only in compiler.c's own SyntaxLocal\n    table -- fixed with the second thread-local above, set by\n    compile_let_syntax/compile_define_syntax around each\n    compile_time_eval call.\n  - sr_gensym's fresh-name counter was a plain, unsynchronized static\n    long -- a genuine cross-thread race (curry's actors run in real\n    detached POSIX threads and can expand macros concurrently),\n    confirmed to produce actual gensym collisions under concurrent\n    load. Fixed with _Atomic long + atomic_fetch_add, matching\n    actors.c's own next_actor_id pattern.\n\nBoth fixes have dedicated regression tests. All exception-safety\nsave/restore sites use SCM_PROTECT, matching this codebase's\nestablished pattern elsewhere.\n\nlib/curry/modules/srfi/s26/cut.scm is the SRFI-26 reference\nimplementation verbatim (public domain, Sebastian Egner / Al\nPetrofsky) -- no curry-specific changes, just the export list needed\nto work around curry's syntax-rules not being hygienic across\ndefine-library boundaries (documented already in\ndocs/reference/writing-a-module.md's own \"macro-expansion gotcha\").\n\n69 tests in tests/syntax_rules_tests.scm (up from 30), 11 new in\ntests/srfi_s26_cut_tests.scm, all 91 ctest suites passing.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix: rename src/features.{c,h} to curry_features.{c,h}\n\n`src/features.h` shadowed glibc's own foundational system header of\nthe exact same name (`<features.h>`, which defines `__GLIBC_USE` and\nfriends and is transitively included by nearly everything, including\nBoehm GC's own headers). On Linux, with `src/` in the include search\npath ahead of the system include dirs, `#include <features.h>` from\ngc/gc_config_macros.h resolved to curry's own file instead of glibc's,\nbreaking the build everywhere that transitively includes <gc/gc.h> --\nwhich is nearly every translation unit. Never surfaced locally\n(macOS/BSD libc has no header by this name), only caught by CI's\nUbuntu build.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(syntax-rules): protect Akkadian special-form synonyms from hygiene renaming\n\nsr_is_protected only checked a symbol against sr_protected_keywords\n(canonical English special-form names, from symbol_list.h) and\nenv_lookup_slot(def_env, ...). An Akkadian/cuneiform special-form\nsynonym -- šumma for if, epēšum for lambda, etc. -- is neither: it's\nresolved to its canonical English symbol only by lang_translate()\n(lang_registry.h) at eval/compile dispatch time, never stored as an\nenvironment binding the way a procedure alias (rēšum for car) is by\nbuiltins.c's startup loop. Without this, such a synonym used inside a\nmacro's own template -- a genuine free reference to a real special\nform, not a template-introduced binder -- got incorrectly renamed to\na gensym and broke.\n\nCaught by CI (Ubuntu build), not local testing: akkadian_tests.scm's\nown define-syntax/syntax-rules translit coverage defines a macro\n(my-and) whose template uses šumma as if, and curry's own day-to-day\nscripts are almost always written in English, so this never surfaced\nlocally. Fixed by also checking sr_protected_keywords against\nlang_translate(sym), so an Akkadian synonym that resolves to a\nprotected special form is itself protected too.\n\nNew regression test in tests/syntax_rules_tests.scm mirrors the exact\nakkadian_tests.scm scenario. All 91 ctest suites still passing.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(tests): guard (curry mqtt) as optional in akkadian_tests.scm\n\nUnlike ldap (already guarded a few sections above, since\nBUILD_MODULE_LDAP is off by default), the mqtt block imported\nunconditionally, assuming the module was always built. It isn't: mqtt\nalso requires libpaho-mqtt installed, which neither this repo's CI\nrunners (confirmed via cmake's own \"MQTT module disabled\" configure\noutput on both macos-latest and ubuntu-latest) nor every local dev\nmachine will have. The unconditional (import (curry mqtt)) threw an\nuncaught module-not-found error there, aborting the whole script mid-run\n-- not a graceful per-check failure, the test harness just saw a crash.\n\nWrapped in the same guard-and-SKIP pattern already used for ldap in\nthis file. Confirmed the pattern works correctly in this exact build\n(ldap itself isn't built locally either, and already SKIPs cleanly);\nwith mqtt actually built locally, behavior is unchanged (all 1738\nakkadian assertions still pass).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(numeric): normalize NaN formatting to a literal \"nan\" everywhere\n\nnum_flonum_to_shortest_cstr delegated NaN formatting to %g, which\nfaithfully prints the NaN's own sign bit (\"-nan\" vs \"nan\") -- but\nIEEE 754 doesn't specify what sign/payload a computation like 0.0/0.0\nproduces, so the same source expression can compile to a\npositive-signed NaN on one platform/compiler and a negative-signed one\non another. Confirmed via CI: this codebase's own NaN round-trip test\npassed on macOS and failed on Linux for exactly this reason, for a\nvalue neither platform's C code actually chooses the sign of.\n\ncurry's own display convention doesn't distinguish signed NaNs at all\n(unlike +inf/-inf, where the sign is meaningful and IEEE-754-unambiguous,\nstill handled via %g) -- NaN is now always spelled as a literal \"nan\",\nindependent of the underlying bit pattern, on every platform.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* test(actors): add CTest TIMEOUT safety net for rare CI hang\n\nCI (Ubuntu Debug, PR #22) once hung indefinitely on the actors test\nunder full-suite ctest -j parallelism -- it spawns 2000 real POSIX\nthreads as a closure-upvalue-race regression test. Reproduced the same\nfull-suite run 9 more times (locally, in an Apple Container Ubuntu VM\nwith matching 4-core/Debug config, both isolated and under full-suite\ncontention) with zero recurrence, so this looks like rare\nnon-deterministic scheduling-timing flakiness rather than a\ndeterministic deadlock -- not something to chase further without\nanother occurrence to get a live backtrace from.\n\nA 120s CTest TIMEOUT turns any future recurrence into a fast, clearly\ndiagnosed CTest TIMEOUT failure instead of a silent 15+ minute CI hang\nthat has to be caught and cancelled by hand.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-14T06:27:08+10:00",
          "tree_id": "85c98d27d9575c710020641f1c487a0d867aeb63",
          "url": "https://github.com/deconstructo/curry/commit/15fec7a314bdced02596776c875dd09f2f1d95b8"
        },
        "date": 1786652882834,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 13.384,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.999,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.586,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 25.281,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 111.994,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 224.371,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 52.83,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 73.282,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 58.164,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "7717b4ae355e6cca87423588e370f6f7cda1c516",
          "message": "feat(symbolic): implement symbolic inequalities (<, <=, >, >=) (#24)\n\n* feat(symbolic): implement symbolic inequalities (<, <=, >, >=)\n\nMotivated by Fredrik Johansson's \"Things I would like to see in a\ncomputer algebra system\" (https://fredrikj.net/blog/2022/04/things-i-would-like-to-see-in-a-computer-algebra-system/),\npoint #12: \"Analysis-oriented CASes are generally good at manipulating\nequalities and limits, but strangely poor at manipulating\ninequalities.\" Before this, `(symbolic x) (< x 5)` raised a confusing\n\"exact integer required, got non-numeric value\" instead of building a\nsymbolic expression the way `+`/`*`/`sin`/etc already do.\n\n<, <=, >, >= now lift over symbolic values in the 2-argument case:\ndecided (returns #t/#f) when both operands are numeric, when they're\nstructurally identical (reflexive case, reusing the existing sx_equal\nhelper), or when one side is a sign-flagged sym-var ('positive/\n'negative) compared against a plain number -- otherwise a genuine\nsymbolic comparison expression like (< x 5), printable via\nsym->infix/sym->latex and substitutable like any other symbolic node.\n\nNew: SX_LT/SX_LE/SX_GT/SX_GE op symbols and sx_lt/sx_le/sx_gt/sx_ge\nconstructors in symbolic.{h,c}, mirroring the existing sx_sign pattern\n(decision logic lives inside sx_simplify's dispatch, not the\nconstructors, so re-simplifying after substitute decides it too, not\njust at construction time). A new NUM_CMP_ORD macro in builtins.c\nhandles the symbolic dispatch and, for 3+-argument calls with any\nsymbolic operand, raises a clear error instead of unsupported chain\nsemantics (no design for what e.g. `1 < x < 5` even prints as).\nPrinter support (infix + LaTeX) in symbolic_print.c.\n\nBonus: sym->markdown wraps sym->latex's output in $...$/$$...$$, the\nmath-delimiter convention GitHub-flavored Markdown/Jupyter/Pandoc/\nObsidian all agree on -- point #17 of the same post (\"publication-\nquality output... should not even be particularly hard\").\n\nExplicitly out of scope (documented in docs/reference/symbolic.md):\nno general expression-level sign inference, no bound assumptions\nbeyond sign, no 3+-arg symbolic chains, = and != not covered (same\nunderlying gap, different judgment call on when #f is safe to return).\n\nIndependently code-reviewed and security-reviewed (fresh subagents, no\nshared context) before landing. Two real findings fixed:\n- The 3+-arg error path only checked the CURRENT pair for a symbolic\n  operand, interleaved with the pairwise comparison loop -- an earlier\n  numeric pair's #f could short-circuit the loop before it ever\n  reached the symbolic pair, so e.g. (< 5 1 x) silently returned #f\n  instead of raising, contradicting the documented guarantee. Fixed by\n  scanning the whole call for a symbolic operand up front.\n- The error message stringified the macro's internal C token (#op,\n  e.g. \"lt\") instead of the Scheme-visible operator name, so a user\n  who wrote `>` saw \"gt: symbolic comparison...\". Fixed by threading\n  the actual operator spelling through the macro explicitly.\n\nAlso fixed one pre-existing doc inaccuracy noticed in review (not\nintroduced by this change): docs claimed differentiating a comparison\nraises an error; it actually stays unevaluated, same as any other\noperator the differentiator has no rule for.\n\n32 new checks in tests/numeric_ext_tests.scm (406 total in that file,\n0 failed), including a regression test for both review findings above.\nFull suite: 91/91 ctest passing.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* perf(symbolic): avoid redundant vis_symbolic re-scan on the 2-arg comparison hot path\n\nThe 3+-arg symbolic-operand fix (previous commit) added a whole-call\nscan for a symbolic operand ahead of the comparison loop, but that\nscan ran unconditionally -- even for the ac==2 case, which had already\njust checked both operands for symbolic-ness one line above. Every\nordinary (< a b) call (the hottest path here by far -- e.g. fib's\n`(< n 2)`) was paying for 4 vis_symbolic checks instead of 2.\n\nInvestigated after CI's benchmark gate flagged a 1.3-1.8x regression\nacross ALL benchmarks simultaneously, including ones (list-build-walk,\nflonum-loop, cont-capture) that only use `=`, which this feature never\ntouched -- a same-machine main-vs-branch comparison (Release build,\naveraged over 3 runs) showed that alert was dominated by CI-runner\nnoise: `=`-only benchmarks were statistically identical, and the real,\nreproducible cost was ~6-8% on fib specifically, nowhere near 1.3x.\nRestructuring so the ac==2 case returns before the 3+-arg scan runs\nbrings alloc-churn/tak/count-down back to noise-level parity with\nmain; fib retains a small ~6-8% cost inherent to adding any check to\na hot comparison dispatch, which is an acceptable, disclosed tradeoff\nfor the feature working at all.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-14T15:39:18+10:00",
          "tree_id": "5b2648541c8c9593615521f69aa7dd835d0a29f9",
          "url": "https://github.com/deconstructo/curry/commit/7717b4ae355e6cca87423588e370f6f7cda1c516"
        },
        "date": 1786686013327,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.957,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.068,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.045,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.286,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.093,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 370.116,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 73.294,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.709,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 98.78,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "aeaf8060eee70bb07575eeea115599b0c5b84419",
          "message": "feat: implement SRFI-14 (Character-Set Library) (#23)\n\nFollow-up to SRFI-26 (cut/cute) from the same session's SRFI-1/26/279\nsurvey -- SRFI-14 was recommended but deprioritized in favor of the\nsyntax-rules hygiene fix and a subsequent CI investigation. This\nimplements the full public API.\n\nA char-set is a curry-native (not a port of SRFI-14's own bitstring-\nbased reference implementation) record wrapping a sorted, non-\noverlapping vector of inclusive codepoint ranges -- chosen because\ncurry chars span the full Unicode codepoint space (0..#x10FFFF minus\nthe surrogate gap), where a bitmap or per-char hash-table would be\nenormous for sets like char-set:letter (~1M members). Range vectors\ngive char-set-contains? an O(log n) binary search and let the\nset-algebra ops run as simple sweeps over sorted ranges.\n\nAdds one new C builtin, unicode-property-ranges, exposing the existing\ngenerated Unicode classification tables (src/unicode_tables.c -- the\nsame ones char-alphabetic?/char-numeric?/etc already use) as Scheme\nrange data, so the full-Unicode predefined sets (char-set:letter,\nchar-set:digit, char-set:whitespace, char-set:upper-case,\nchar-set:lower-case) are built from the real tables in O(1) instead of\nscanning ~1.1M codepoints at the Scheme level. Everything else\n(punctuation/symbol/iso-control/blank/hex-digit/ascii/graphic/\nprinting/title-case) is ASCII-only (or empty for title-case), since\ncurry has no generated Unicode General-Category tables for those\nclasses -- documented in the module's header comment rather than\nsilently mis-classifying non-ASCII input.\n\nIndependently code-reviewed and security-reviewed (fresh subagents,\nno shared context) before landing. Two real findings applied:\n- char-set-map inherited char->integer's codebase-wide lack of a\n  type check, so a buggy mapper proc returning a non-char silently\n  produced a garbage-but-in-range codepoint instead of a clean error.\n  Added a char? check in %char->cp, the one chokepoint every codepoint\n  entering a char-set passes through.\n- char-set-union/-union! folded through %ranges-union (a full re-sort)\n  once per operand, O(k^2 log k) for k operands instead of O(k log k).\n  Fixed by collecting all operands' ranges and normalizing once.\n  char-set-xor is NOT safe to flatten the same way (parity-sensitive,\n  not just coverage), so it keeps its sequential pairwise fold --\n  documented why in a comment so it isn't \"fixed\" the same way later.\n\nNo memory-safety issues found: the new C builtin validates its string\nargument and raises cleanly (via scm_raise, a real longjmp, not a\nfallthrough) on an unknown property name before touching the\notherwise-uninitialized table pointer/count; ucs-range->char-set\nclamps arbitrarily large or reversed integer bounds to O(1) cost via\nmin/max rather than materializing the requested range.\n\n69 tests (tests/srfi_s14_char_sets_tests.scm), including full-Unicode\ncoverage above the BMP (a real >0xFFFF classification-table entry, not\njust the surrogate-exclusion logic char-set:full already covered).\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-14T15:50:07+10:00",
          "tree_id": "c300c23eb093f259841f6b2d410dab0219882a51",
          "url": "https://github.com/deconstructo/curry/commit/aeaf8060eee70bb07575eeea115599b0c5b84419"
        },
        "date": 1786686647903,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 25.357,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.464,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.645,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.152,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 223.78,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 415.641,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 76.33,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 136.705,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 111.301,
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
          "id": "7d463a1966fe86f545e5fe1c801695b8b33bfd94",
          "message": "chore(formula): update sha256 for v1.20.0 tarball",
          "timestamp": "2026-08-14T15:55:48+10:00",
          "tree_id": "cbc7d235c759c895265b8c6e7c63c6c520bcaf01",
          "url": "https://github.com/deconstructo/curry/commit/7d463a1966fe86f545e5fe1c801695b8b33bfd94"
        },
        "date": 1786687016274,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.172,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 27.792,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.35,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.921,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 139.637,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 270.345,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 62.169,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 92.917,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 73.677,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "4520cb2b5327995fae7838c417ca250a6d905f24",
          "message": "feat: add procedure introspection (procedure-name/-arity/-arglist/-file/-line/-lambda/-closure) (#25)\n\nCloses a real, previously-documented gap: (srfi s279 inspect)'s own\nheader comment said \"curry exposes no procedure-name/arity/arglist\nintrospection to Scheme at all yet\". Curry has three procedure\nrepresentations with different available metadata -- tree-walker\nclosures (T_CLOSURE: params/body/name directly, no source-location\ntracking at all), bytecode-VM closures (T_BCCLOSURE, wraps a Chunk\nthat already carries name/arity/source_name/lines/src_lambda for the\ndebugger's sake), and C primitives (name/min_args/max_args only, no\nsource form or captured closure to report). Each new primitive\ndispatches on which representation it got and reports #f (or '() for\nprocedure-closure) when a property genuinely doesn't exist for that\nrepresentation, rather than guessing -- matching SRFI-279's own\n\"omit unsupported properties\" rule, which (srfi 279)'s new\n%procedure-properties wires these into directly.\n\nFound and fixed one real pre-existing bug along the way: Chunk.src_lambda\n(procedure-lambda/procedure-arglist's data source, also used for\ntiered-JIT hot-swap) was never persisted to the .scc bytecode cache, so\na script's FIRST run (a cache MISS, compiled fresh in memory) reported\nit correctly but every subsequent run (a cache HIT, loaded from disk)\nsilently lost it and returned #f -- discovered because the new SRFI-279\ntests failed only on their second run. Fixed by serializing src_lambda\nthrough the existing generic write_const/read_const machinery (already\nhandles arbitrary S-expressions) and bumping SCC_FMT_VER 5->6 so every\npre-existing cache is cleanly invalidated and recompiled rather than\npartially misread.\n\nIndependently code-reviewed and security-reviewed (fresh subagents, no\nshared context) before landing. Two real findings fixed:\n- closure_params_arity walked a closure's params list with no cycle\n  guard. Confirmed live and reachable from pure Scheme (no sandbox\n  needed): (eval (list 'lambda circular-params body) env) builds a\n  real T_CLOSURE with circular params (nothing validates it eagerly at\n  construction time), and calling procedure-arity on it hung forever --\n  a genuine DoS reachable anywhere untrusted-influenced Scheme can reach\n  a REPL helper, notebook, MCP tool, or LSP hover that calls\n  procedure-arity/inspect-properties. Fixed with Floyd's cycle\n  detection (tortoise/hare) rather than a hard iteration cap, so\n  legitimately long finite parameter lists still work correctly.\n- procedure-closure's \"don't dump a huge shared scope\" guard checked\n  pointer-identity against GLOBAL_ENV specifically, missing every\n  OTHER root frame -- namely every (curry X)/SRFI module's own\n  env_new_root() frame (every define-library body runs in one, per\n  this project's own module-system convention). A closure returned\n  from ANY exported procedure of ANY shipped module leaked its entire\n  module scope through procedure-closure: verified live at ~1933\n  bindings (every imported builtin, Akkadian/cuneiform aliases, and\n  the real process command-line-args) from one ordinary two-line test\n  module. Made worse by being reachable transitively through (srfi\n  279)'s inspect-properties/inspect-describe, which calls\n  procedure-closure unconditionally -- an innocuous-looking \"describe\n  this object\" debugging call was the actual leak vector. Fixed by\n  generalizing the guard to env->parent == NULL (any root frame, not\n  just the one specific GLOBAL_ENV instance), which correctly covers\n  both cases while still reporting real captures for nested lambdas.\n\nTwo lower-severity findings documented rather than fixed (both\npre-existing risk shapes in code this change doesn't otherwise touch,\nnot new problems introduced by it): write_const's unbounded recursion\non long cdr chains is now reachable via arbitrarily-long lambda bodies,\nnot just short quoted literals (a bigger, separate iterative-rewrite\ntask); procedure-closure's '() return conflates \"genuinely no captures\"\nwith \"this representation can't report captures\" (matches SRFI-279's\nown established omission-collapsing convention elsewhere).\n\n31 new checks in tests/srfi_s279_inspect_tests.scm (123 total, 0\nfailed) including a regression test for the module-scope leak (a\ndefine-library-scoped closure's procedure-closure must come back\nempty, not the whole module frame). 2 new checks in tests/test_cli.sh\n(69 total, 0 failed) for the .scc cache MISS/HIT src_lambda\nregression. Full suite: 92/92 ctest passing.\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-14T16:30:39+10:00",
          "tree_id": "082c54aa887d47bc2475fc132a085528e1941d7d",
          "url": "https://github.com/deconstructo/curry/commit/4520cb2b5327995fae7838c417ca250a6d905f24"
        },
        "date": 1786689088326,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.341,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.501,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.307,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.095,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 190.669,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 369.724,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.399,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.384,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.96,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "5633820760d00ee30122f6c26f730b6a3acf6ef1",
          "message": "feat: close (srfi 279) gaps -- char-sets, universal id/size/type, symbol-value, char-name, hash-table introspection (#26)\n\nCloses items 1, 2, and 4 of the SRFI-279 gap list from this session's\nearlier codebase review (item 3, rtd-accessors/-mutators/-predicate/\n-constructor, needs real RecordType struct + compiler codegen changes\nand is scoped as its own separate follow-up, alongside ports and\nSRFI-4/160).\n\n- Char-sets (srfi 14, newly available in this codebase): char-set-size,\n  char-set->list, char-set->string, and char-set-name (a reverse lookup\n  against the predefined sets via char-set= structural equality, since\n  SRFI-14 char-sets are otherwise anonymous). char-set:title-case is\n  deliberately not its own lookup entry -- curry has no titlecase table\n  at all, so it's permanently empty and structurally indistinguishable\n  from char-set:empty by char-set=, and honestly reports that name\n  rather than being given an unverifiable one of its own.\n\n- Universal object properties (id/size/type -- location deliberately\n  omitted, see its own comment): two new C primitives, %object-id\n  (a hash of the pointer via the existing val_hash SET_CMP_EQ path,\n  not the raw address, to avoid leaking memory layout) and\n  %object-size (byte footprint for String/Vector/Bytevector/Pair only,\n  #f -- omitted, not guessed -- for everything else). type is computed\n  in Scheme from the same predicate order inspect-properties' own\n  dispatch already uses.\n\n- symbol-value: a plain global-environment lookup (guarded eval),\n  distinguishing \"unbound\" from \"bound to a real #f\" via a private,\n  unexported sentinel value nothing outside the library can obtain a\n  reference to.\n\n- char-name: curry's own 9 R7RS named-character reader vocabulary\n  (space/newline/tab/return/null/escape/delete/alarm/backspace),\n  reverse-looked-up.\n\n- hash-table-equivalence-function/-hash-function: (srfi 69)'s own\n  accessors (which already return real procedure objects) piped\n  through procedure-name (added in a prior PR) to get the symbol name\n  SRFI-279 actually specifies. hash-table-weak?/-mutable?: fixed\n  #f/#t constants matching curry's actual hash-table semantics (no\n  weak-reference variant, always mutable).\n\nIndependently code-reviewed and security-reviewed (fresh subagents, no\nshared context) before landing. Two real bugs found and fixed:\n- %object-size for strings used live content length (`len`) instead\n  of allocated capacity (`orig_cap`), silently under-reporting any\n  string whose capacity exceeds its content -- and, worse, any string\n  that had a width-changing string-set!/string-copy! (ASCII -> multi-\n  byte or back), which reallocates onto a separate `ext` buffer while\n  the ORIGINAL inline block stays allocated as dead weight (Boehm GC,\n  no realloc-in-place). Verified live: a 10000-byte string's real\n  footprint roughly doubles after one such edit; the old formula\n  reported it as nearly unchanged. Fixed using orig_cap for the inline\n  block plus the ext buffer's own size (every ext-setting call site in\n  this file allocates it as exactly len+1 bytes, confirmed by reading\n  both sites, not assumed).\n- %object-id is not actually stable under the experimental, opt-in\n  `--gc generational` backend, which promotes/copies live objects\n  during minor collections -- changing the pointer the id hashes from\n  mid-process. Verified live (same object, same process, id changes\n  after enough allocation to trigger a promotion) against the default\n  Boehm backend (stable for the whole process, as claimed). Not\n  redesigned to fix -- giving every heap type a real GC-move-\n  independent identity slot is disproportionate to this feature for\n  the sake of one experimental, non-default backend -- documented as\n  an explicit, honest limitation in the primitive's own doc comment\n  instead of the previous unconditional \"stable\" claim.\n\nOne informational finding from security review incorporated into the\ndoc comment rather than requiring a code change: %object-id's actual\nprotection against address recovery is the 32-bit output truncation,\nnot one-wayness of the underlying hash mix (which is a real, invertible-\nin-principle avalanche finalizer) -- a probabilistic speed bump, not a\nhard guarantee, adequate for curry's scripting-language threat model\nbut worth writing down rather than leaving implicit.\n\n31 new/updated checks in tests/srfi_s279_inspect_tests.scm (155 total,\n0 failed), including regression coverage for both fixed bugs (the\nstring ext-buffer size fix) and one pre-existing test's hardcoded\nproperty count updated (2 -> 4) to account for the new universal id/\ntype properties always being present. Full suite: 92/92 ctest passing.\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-14T17:03:49+10:00",
          "tree_id": "6e74a8ca479820a4685d3659879323f31b3f1de3",
          "url": "https://github.com/deconstructo/curry/commit/5633820760d00ee30122f6c26f730b6a3acf6ef1"
        },
        "date": 1786691069753,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.899,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.342,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.001,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.135,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 189.61,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 367.588,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.157,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.312,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 98.708,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "11b58544a29a81d5a89c0c961f8ed95205d9f2f3",
          "message": "feat: add port introspection (textual-port?/binary-port?/port-line/-position/-file-descriptor) and wire into (srfi 279) (#27)\n\nCloses the \"ports\" gap from the SRFI-279 gap list.\n\nNew standalone primitives, genuinely useful beyond just this SRFI:\n- textual-port?/binary-port?: real R7RS section 6.13.1 procedures\n  curry previously had no way to call at all (the underlying C helper,\n  port_is_binary, already existed in src/port.c -- just never wrapped\n  for Scheme).\n- port-line: exposes the line-tracking curry's reader already\n  maintains internally for its own backtraces.\n- port-position: for string/bytevector-backed ports, reads the exact\n  byte offset directly out of curry's Port struct; for FILE*-backed\n  ports, ftell(3).\n- port-file-descriptor: fileno(3) on the underlying FILE* for\n  FILE*-backed ports; #f for string/bytevector ports (no fd at all).\n\nWired into (srfi 279)'s inspect-properties as %port-properties:\nport-open?, port-direction ('input/'output/'both), port-type\n('textual/'binary), port-line, port-position, port-file-descriptor,\nport-encoding (fixed 'UTF-8 for textual ports -- curry really is\nUTF-8-only throughout), port-buffer, get-output-string/\nget-output-bytevector. port-file and port-column are omitted\nentirely, not merely unwired: curry's Port struct never stores an\nopening file path, and never tracks column at all.\n\nOne notable design point: curry's own pre-existing get-output-string/\nget-output-bytevector only check the PORT_STRING flag internally, not\ndirection -- calling get-output-string on an INPUT string port doesn't\nraise at all, it just returns the input content. SRFI-279 specifies\nthese properties for output ports specifically (\"for string and\nbytevector output ports\"), so %port-properties gates get-output-string/\n-bytevector and the new port-buffer property on (output-port? object)\nexplicitly in Scheme, rather than trusting curry's own primitives to\nenforce that boundary.\n\nIndependently code-reviewed and security-reviewed (fresh subagents, no\nshared context) before landing. Both independently found the same\ncritical bug:\n- port-position and port-file-descriptor called ftell(3)/fileno(3)\n  directly on a FILE*-backed port's raw C FILE* with no PORT_CLOSED\n  check. port_close (src/port.c, pre-existing, unchanged here)\n  fclose()s a non-std-stream port and sets its u.fp to NULL on close,\n  but closing a port doesn't change its own type tag -- port? still\n  says true, so nothing upstream filtered a closed port out before it\n  reached these primitives. Calling ftell/fileno on a NULL FILE* is\n  undefined behavior; verified live to reliably segfault the process\n  (exit 139) on this platform. Reachable from completely ordinary\n  Scheme code with no unsafe/FFI involved -- including transitively\n  through plain inspect-properties/inspect-describe on a closed port,\n  an entirely unremarkable thing to introspect (e.g. debugging why/\n  when a port closed). Fixed by checking PORT_CLOSED (and u.fp != NULL\n  as a second guard) before either libc call, returning #f -- the same\n  \"not meaningful here\" convention already used for the ftell-fails\n  and no-fd-at-all cases, not a new one invented for this.\n- A related fix along the way: port-position originally used the\n  string port's read-cursor field (u.str.pos) unconditionally, which\n  is exclusively an INPUT cursor -- port_write_char/port_write_string\n  (src/port.c) only ever touch u.str.len, never pos, so an output\n  port's position silently stayed 0 after every write. Fixed by\n  selecting the field based on PORT_OUTPUT.\n\nAlso closed a real test-coverage gap the reviews flagged: the original\ntest suite only exercised string/bytevector ports, never a real\nFILE*-backed port (open or closed) -- exactly where the crash lived,\nso it wasn't caught before review. New tests cover both.\n\n41 new checks in tests/srfi_s279_inspect_tests.scm (191 total, 0\nfailed), including regression coverage for both the closed-port crash\nand the output-position field bug. Full suite: 92/92 ctest passing.\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-14T17:32:43+10:00",
          "tree_id": "cc9c048ee40577ecc54a313d70a0bcc725133a4f",
          "url": "https://github.com/deconstructo/curry/commit/11b58544a29a81d5a89c0c961f8ed95205d9f2f3"
        },
        "date": 1786692811088,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.492,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.409,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.187,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.85,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 198.361,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 366.272,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.923,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.906,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.793,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "91134113524b9c984796f1b6a873b9742c0a150c",
          "message": "feat(typedvec): SRFI-4 core typed vectors (u8..s64) + (srfi 4) wrapper (#28)\n\n* feat(typedvec): add SRFI-4 core typed vector module (u8..s64)\n\nNew T_TYPEDVEC heap type + modules/typedvec/typedvec.c covering the 8\ninteger SRFI-4 kinds (u8/s8/u16/s16/u32/s32/u64/s64); f64vector is\ndeliberately excluded since (curry f64vector) already ships it.\nRegisters 12 generic operations x 8 kinds via a single parameterized\nC implementation rather than duplicating per-kind code.\n\n- src/object.h: T_TYPEDVEC tag, TVKind enum, TypedVec struct, tv_elem_size\n- src/gc_gen.c: size/type_has_ptrs/scan_object cases for the generational\n  (experimental, non-default) GC backend\n- src/port.c: #u8(...)/#s64(...)/etc external representation in scm_write,\n  mirroring the existing #f64(...) case\n- CMakeLists.txt: BUILD_MODULE_TYPEDVEC option (default ON)\n\nu64/s64 values round-trip exactly as bignums past signed-long range;\nrange checking verified (out-of-range u8vector-set! raises).\n\n* feat(srfi4): add (srfi 4) uniform-vectors wrapper, tests, docs\n\nCombines (curry typedvec) (u8..s64, this session's new module) with\nthe pre-existing (curry f64vector) module into one SRFI-4 surface,\navailable as (srfi 4), (srfi srfi-4), and (srfi s4 uniform-vectors)\nper the codebase's existing SRFI-shim convention. f64vector-copy and\nf64vector-append are not re-exported since (curry f64vector) does not\nimplement copy-into or append; f32vector is out of scope (curry has\nno native single-precision float type).\n\n- tests/typedvec_tests.scm: 34 checks covering all 8 integer kinds,\n  range checking, u64/s64 exact-bignum round-tripping, copy/append/\n  fill, and the new #u8(...)/#s64(...) external representation\n- docs/reference/module-typedvec.md, docs/reference/srfi/s4.md\n- registered in docs/reference/modules.md and srfi/index.md\n\n* fix(typedvec): address independent code+security review findings\n\nBoth a fresh code-review subagent and a fresh security-review subagent\nindependently examined the SRFI-4 typedvec commits with no shared\ncontext, and each found real bugs:\n\n- CRITICAL: the printer's #u8(...)/#s8(...)/etc external representation\n  collided with existing reader syntax -- #u8( is already R7RS\n  bytevector literal syntax (reading a written u8vector silently\n  produced a bytevector instead, a type-confusion bug), and bare #s...\n  is already the sexagesimal-number reader (reading a written s8vector\n  through s64vector silently corrupted the rest of the read stream\n  instead of raising). Fixed by suffixing every prefix with \"vec\"\n  (#u8vec(...), #s64vec(...), etc), which neither reader path's literal\n  character match can succeed on -- both now raise a clean read error\n  on read-back instead of misparsing, matching the pre-existing\n  #f64(...) representation's own same non-reader-syntax limitation.\n- tv_range()'s end argument and fn_typedvector_copy_bang's fend both\n  called vunfix() directly without the vis_fixnum check tv_idx() uses\n  everywhere else, so a non-fixnum argument (e.g. #t) was silently\n  reinterpreted as a small integer instead of raising a type error.\n  Fixed by routing both through tv_idx().\n- make-TAGvector validated only n < 0, then silently truncated to\n  uint32_t -- (make-u8vector 4294967297 7) produced a 1-element vector\n  instead of raising. Fixed with an explicit upper-bound check.\n- %svector-append accumulated per-argument lengths into a uint32_t\n  with no overflow check; for a combined length exceeding UINT32_MAX\n  (requires several GB of typed vectors) this wraps to a small value,\n  under-allocates the result, and the copy loop then writes past the\n  allocation -- a real heap buffer overflow, just not reproducible in\n  a normal sandbox. Fixed by accumulating in uint64_t and rejecting\n  before truncation.\n- fn_typedvector_copy_bang's own at + n > to->len bounds check had the\n  same uint32_t-overflow shape; now computed in size_t.\n\nAdded regression tests for all of the above plus read-back checks\nconfirming #u8(...) still resolves to a real bytevector and the new\n#u8vec(...)/#s8vec(...) forms raise cleanly rather than misparsing.",
          "timestamp": "2026-08-14T17:49:08+10:00",
          "tree_id": "8ecebaa0795d81ea42b176f0d7e07810878ddfcc",
          "url": "https://github.com/deconstructo/curry/commit/91134113524b9c984796f1b6a873b9742c0a150c"
        },
        "date": 1786693818342,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.399,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.587,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.839,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.043,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 144.811,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 281.613,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 63.359,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 90.434,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 75.487,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "36b1c08a9197e0ae8310a4258f22f6113fb184b3",
          "message": "feat(srfi160): extended uniform-vector ops (map/fold/filter/comparator/generator) for all 9 kinds (#29)\n\n* feat(srfi160): add extended SRFI-160 uniform-vector ops for all 9 kinds\n\nLayers the extended SRFI-160 operation set -- higher-order iteration,\nsearching, folding, comparators, and generators -- on top of the\nSRFI-4 core shipped in #28, for all 9 kinds (u8/s8/u16/s16/u32/s32/\nu64/s64/f64). Entirely pure Scheme: no new C module, built on the\nexisting (curry typedvec)/(curry f64vector) primitives plus the\nalready-shipped (srfi 128) comparators and (srfi 158) generators.\n\n- lib/curry/modules/srfi/s160/uniform-vectors.scm: -empty?, -=,\n  -swap!, -reverse!, -reverse-copy, -map(!), -for-each, -count,\n  -index(-right), -skip(-right), -any, -every, -filter, -remove,\n  -partition, -fold, -fold-right, -concatenate, -unfold(-right),\n  a comparator instance, and a generator, per kind; re-exports the\n  SRFI-4 base ops too so this one library is self-sufficient\n- (srfi 160) / (srfi srfi-160) bare-number shims, matching this\n  codebase's existing SRFI naming convention\n- also fixes a real gap in already-merged #28: (srfi 4)'s doc and\n  export list incorrectly claimed (curry f64vector) has no\n  f64vector-append; it does (fixed 2-arg arity, unlike the other 8\n  kinds' N-ary append) and just wasn't re-exported -- now is\n\nCaught during manual testing (all fixed before commit, since this\nwas generated code -- one generator script produces the same shape\nof definition for all 9 kinds, so a bug in the template is a bug in\nall 9):\n- TAGvector= had a real infinite loop: the N-ary equality chain's\n  recursive step called (loop rest) instead of (loop (cdr rest)),\n  so any 3+-argument call that reached its second comparison never\n  terminated\n- TAGvector-fold-right passed kons the accumulator in the same\n  position as TAGvector-fold ((kons acc e1 e2 ...)); SRFI-133/\n  SRFI-160's actual convention has fold-right's kons take the\n  elements first and the accumulator last, which matters for a\n  non-commutative kons like cons\n- TAGvector-comparator values were unusable without a separate\n  (srfi 128) import, since comparator?/=?/<?/etc weren't re-exported\n\ntests/srfi_160_tests.scm: 54 checks -- full coverage on u8, plus\ns64 (bignum-boundary values) and f64 (float) spot checks, since the\nsame generated code is shared across all 9 kinds. Regression tests\nadded for both bugs found above. Full ctest suite: 94/94 pass.\n\n* docs: catch up CHANGELOG.md through v1.20.0\n\nThe changelog was stuck at a stale \"1.17.12\" heading (dated the same\nday as the actual v1.18.0 tag -- fixed to match) with three full\nreleases worth of work undocumented since: v1.19.0 (mariadb/postgres\ntype coercion, structured errors, streaming, TLS, LISTEN/NOTIFY, COPY)\nand v1.20.0 (TTS module, cond-expand and (features), SRFI-279, the\nload/include directory-context fix, syntax-rules partial hygiene plus\nSRFI-26, SRFI-14, symbolic inequalities, NaN formatting, and the\nfeatures.h glibc header collision fix).\n\n* fix(srfi160): address independent code+security review findings\n\nBoth a fresh code-review subagent and a fresh security-review\nsubagent (no shared context with the writing session) reviewed the\nSRFI-160 commit and each found a real bug:\n\n- f64vector-concatenate aliased its input instead of copying it for a\n  single-element list: (fold-left f64vector-append (car vs) '())\n  short-circuits to (car vs) unchanged with no f64vector-append call\n  at all, so (f64vector-concatenate (list v)) returned v itself --\n  later mutating the \"new\" vector silently corrupted the original.\n  Every other kind's -concatenate is (apply TAGvector-append vs),\n  which always allocates fresh even for one argument, so this was an\n  f64-specific regression from its special-cased pairwise-fold\n  implementation (needed because (curry f64vector)'s f64vector-append\n  is a fixed 2-arg procedure, unlike the other 8 kinds' N-ary form).\n  Fixed by special-casing the single-element case to f64vector-copy.\n\n- TAGvector-unfold/-unfold-right (all 9 kinds) SIGSEGV'd via C stack\n  overflow at a few thousand elements: (u8vector-unfold ... 5000 0)\n  reliably crashed the process. Root cause is a pre-existing curry\n  core-VM defect -- call-with-values/apply tail calls don't get fully\n  TCO'd inside a define-library body (confirmed: the identical pattern\n  survives millions of iterations at the top level, but crashes at\n  ~2000 inside a library) -- not a logic bug in this new code per se,\n  but every (curry X)/SRFI library is required to be a define-library,\n  so this public API was directly and trivially crashable. Worked\n  around at the library level: call-with-values's receiver is now\n  `list` (an ordinary, non-tail call) instead of a lambda that itself\n  makes the loop's recursive tail call, so the actual recursion\n  happens as a separate, genuinely-tail call afterward. Verified\n  correct and crash-free at 200,000 elements. The underlying core-VM\n  TCO defect remains open and should be tracked/fixed separately.\n\nRegression tests added for both: aliasing (mutate-after-concatenate\non a singleton list) and the crash threshold (200k-element unfold).\ntests/srfi_160_tests.scm: 57 checks, up from 54. Full ctest suite:\n94/94 pass.",
          "timestamp": "2026-08-14T18:22:49+10:00",
          "tree_id": "e5bb1cbfab214fd936b382bc50bfba0639fb11bd",
          "url": "https://github.com/deconstructo/curry/commit/36b1c08a9197e0ae8310a4258f22f6113fb184b3"
        },
        "date": 1786695815095,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.236,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.27,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.064,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.689,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 190.958,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 376.805,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 76.536,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.07,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 100.801,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "252be18be057043b7813e50f655347edc81a659f",
          "message": "feat(srfi279): wire in numeric-vector properties; fix stale docs (#30)\n\n* feat(srfi279): wire in numeric-vector properties; fix stale docs\n\nAdds typedvec-properties to (srfi s279 inspect), covering all 9 SRFI-4\nkinds (u8/s8/u16/s16/u32/s32/u64/s64/f64vector) -- unblocked now that\n(curry typedvec) and (curry f64vector) both exist. No single generic\n\"is this any typed vector\" predicate exists across the 9 kinds (8\nshare one heap type with a kind flag but only expose per-kind\npredicates; f64vector is a wholly separate heap type), so a small\ndispatch table looks up each kind's own predicate/length/->list\nprocedures. Property keys are the real per-kind procedure names (e.g.\n'u8vector-length), matching this module's existing convention of\nnaming a property after the standard procedure that produced it.\n\nAlso fixes documentation that had gone stale as other work landed:\n- The .scm header and docs/reference/srfi/s279.md's \"Deliberately out\n  of scope\" list still claimed no procedure introspection existed and\n  no ports support existed -- both shipped in earlier PRs (#25, #27)\n  and are already wired into inspect-properties, just never reflected\n  in either doc.\n- The \"Supported types\" table was missing rows for port and procedure\n  properties entirely, despite both being implemented.\n- Added an explicit, prominent note that SRFI-279 is still a draft\n  (not finalized) and that this page/module's surface is therefore\n  provisional and may need to change to track the spec -- the\n  existing mention was just a parenthetical in the intro line.\n\ntests/srfi_s279_inspect_tests.scm: 13 new checks covering all 9 kinds\n(including s64/u64 bignum-boundary round-tripping and cross-kind\npredicate isolation), up to 204 from 191. Full ctest suite: 94/94 pass.\n\n* docs(srfi279): note inspect-properties' cost on large sequences\n\nIndependent security review of the typedvec-properties addition\nmeasured (inspect-properties v) on a 50M-element u8vector at ~127s/\n13.4GB peak RSS vs ~2.8s/2.7GB for (u8vector->list v) alone -- about\n45x slower and 5x more memory than the accessor it calls, from\nmaterializing the object as a list and then a second list of (index\nelement) pairs on top of that. Pre-existing behavior shared with\nordinary vector/bytevector properties, not introduced by typed-vector\nsupport, but typed vectors are the type real code uses for large\nbinary/numeric buffers, so more likely to actually be large in\npractice. Not a bug, just worth documenting explicitly.",
          "timestamp": "2026-08-14T18:44:50+10:00",
          "tree_id": "e197f8a11a3b4ec09459e11938b49ed93767390f",
          "url": "https://github.com/deconstructo/curry/commit/252be18be057043b7813e50f655347edc81a659f"
        },
        "date": 1786697144291,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.427,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.637,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.406,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.217,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 204.327,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 378.452,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.957,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.715,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 103.857,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "9f511e742b8e39cb0a5c62c0894b2f6a1fe32439",
          "message": "feat(srfi1): close SRFI-1 list-library gap; fix real fold argument-order bug (#31)\n\n* feat(srfi279): wire in numeric-vector properties; fix stale docs\n\nAdds typedvec-properties to (srfi s279 inspect), covering all 9 SRFI-4\nkinds (u8/s8/u16/s16/u32/s32/u64/s64/f64vector) -- unblocked now that\n(curry typedvec) and (curry f64vector) both exist. No single generic\n\"is this any typed vector\" predicate exists across the 9 kinds (8\nshare one heap type with a kind flag but only expose per-kind\npredicates; f64vector is a wholly separate heap type), so a small\ndispatch table looks up each kind's own predicate/length/->list\nprocedures. Property keys are the real per-kind procedure names (e.g.\n'u8vector-length), matching this module's existing convention of\nnaming a property after the standard procedure that produced it.\n\nAlso fixes documentation that had gone stale as other work landed:\n- The .scm header and docs/reference/srfi/s279.md's \"Deliberately out\n  of scope\" list still claimed no procedure introspection existed and\n  no ports support existed -- both shipped in earlier PRs (#25, #27)\n  and are already wired into inspect-properties, just never reflected\n  in either doc.\n- The \"Supported types\" table was missing rows for port and procedure\n  properties entirely, despite both being implemented.\n- Added an explicit, prominent note that SRFI-279 is still a draft\n  (not finalized) and that this page/module's surface is therefore\n  provisional and may need to change to track the spec -- the\n  existing mention was just a parenthetical in the intro line.\n\ntests/srfi_s279_inspect_tests.scm: 13 new checks covering all 9 kinds\n(including s64/u64 bignum-boundary round-tripping and cross-kind\npredicate isolation), up to 204 from 191. Full ctest suite: 94/94 pass.\n\n* docs(srfi279): note inspect-properties' cost on large sequences\n\nIndependent security review of the typedvec-properties addition\nmeasured (inspect-properties v) on a 50M-element u8vector at ~127s/\n13.4GB peak RSS vs ~2.8s/2.7GB for (u8vector->list v) alone -- about\n45x slower and 5x more memory than the accessor it calls, from\nmaterializing the object as a list and then a second list of (index\nelement) pairs on top of that. Pre-existing behavior shared with\nordinary vector/bytevector properties, not introduced by typed-vector\nsupport, but typed vectors are the type real code uses for large\nbinary/numeric buffers, so more likely to actually be large in\npractice. Not a bug, just worth documenting explicitly.\n\n* feat(srfi1): close SRFI-1 list-library gap; fix real fold bug\n\ncurry's (srfi 1) only implemented a ~30-name subset of the real\n100+-procedure SRFI-1 spec. Adds the missing surface: constructors\n(xcons, cons*, list-tabulate, circular-list), predicates (proper-list?,\ncircular-list?, dotted-list?, null-list?, not-pair?, list=), selectors\n(sixth..tenth, take-right, drop-right, split-at, last), fold/unfold\n(reduce, reduce-right, unfold, unfold-right, pair-for-each, multi-list\nfold/any/every/count/list-index), searching (find, find-tail, span,\nbreak, list-index, member/assoc with the optional equality predicate\nboth R7RS and SRFI-1 specify -- curry's own global member/assoc don't\naccept one), delete-duplicates, append/concatenate/reverse helpers,\nzip/unzip1..5, association-list helpers, and the lset-* set-on-lists\nfamily. Every !-suffixed procedure is a plain alias for its\nnon-destructive counterpart -- SRFI-1 explicitly permits (does not\nrequire) in-place mutation, and this avoids shared-structure hazards\nunder curry's GC'd, immutable-by-convention list style for no real\nbenefit.\n\nReal bug found and fixed while doing this: (srfi 1)'s own `fold` was\nimplemented via fold-left's argument convention (accumulator first),\nnot SRFI-1's actual convention (elements first, accumulator last) --\nsilently wrong for any non-commutative kons. (fold cons '() '(1 2 3))\nwas (((() . 1) . 2) . 3); must be (3 2 1).\n\nThat fix has a real blast radius: lib/curry/modules/srfi/s14/\nchar-sets.scm was deliberately written AROUND the old bug (its own\ncomment said so explicitly), using (lambda (acc x) ...) callbacks\nagainst 10 different fold call sites. All 10 updated to the correct\n(lambda (x acc) ...) order; full codebase grep confirms char-sets.scm\nwas the only consumer relying on the old (wrong) order.\n\nAlso found and fixed a separate, subtler bug while getting `reduce`/\n`member`/`assoc` to actually work through the (srfi 1)/(srfi srfi-1)\nbare-number shims: both shims imported (curry private lang-aliases) --\na plain, non-define-library file whose environment chains up to\nGLOBAL_ENV -- AFTER importing (srfi s1 lists), so any newly-overridden\nname that was ALREADY a core global primitive (member/assoc/reduce all\npredate this library) got silently re-clobbered back to the stale core\nversion by the second import. Fixed by importing lang-aliases first,\nso (srfi s1 lists)'s own bindings win last -- direct (import (srfi s1\nlists)) without the shim was never affected, only the two bare-number\nwrapper libraries.\n\ntests/srfi_1_tests.scm: 70 new checks (new module, previously\nuntested at the (srfi 1) level at all). Full ctest suite: 95/95 pass.\n\n* fix(srfi1): dotted-list? hangs forever on a circular list\n\nIndependent security review (cut short by an account spend limit\nmid-run, but this finding was already confirmed live before it\nstopped) found dotted-list?'s original implementation did its own\nnaive (cdr p) walk with no cycle detection -- (dotted-list? (circular-\nlist 1 2 3)) never returned.\n\nproper-list?/circular-list?/dotted-list?/null-list?/not-pair? are\nexactly the small set of SRFI-1 procedures whose entire purpose is\ncorrectly classifying ANY list shape, including circular ones, without\nhanging -- unlike ordinary list procedures (find, pair-for-each, zip,\netc), which SRFI-1 does not require to handle circular input safely,\nmatching R7RS's own map/for-each. proper-list? and circular-list? were\nalready correctly cycle-safe (delegating to R7RS's cycle-safe list?,\nand a tortoise/hare walk, respectively); dotted-list? is now derived\nfrom those two instead of a third independent manual walk, since a\nlist is exactly one of {proper, circular, dotted}.\n\nRegression test added; full ctest suite: 95/95 pass (two unrelated\nfailures on the first parallel run -- debugger, lsp -- confirmed as\nthis session's established ctest-contention noise pattern, both pass\nstandalone).",
          "timestamp": "2026-08-14T19:33:10+10:00",
          "tree_id": "999777840f421f835955238cb2894a79e3b326bd",
          "url": "https://github.com/deconstructo/curry/commit/9f511e742b8e39cb0a5c62c0894b2f6a1fe32439"
        },
        "date": 1786700039853,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.757,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.73,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.945,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.161,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.047,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 370.3,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.022,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.89,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.58,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "9e7f5dd51c8134132997a96ec7149c7ee923d136",
          "message": "fix(srfi1): restore list* export + fix SIGSEGV-prone recursion (follow-up to #31) (#32)\n\n* fix(srfi1): restore list* to the export list (regression)\n\nIndependent code review found list* silently dropped from (srfi 1)/\n(srfi srfi-1)'s export list during the SRFI-1 gap-closing rewrite --\nit was previously reachable only via the Akkadian-alias chain\n(lang-aliases assumed it was already exported), and the new,\ncarefully-scoped export clause never re-added the plain name itself.\nConfirmed live: importing (srfi 1) into a fresh define-library body\nand calling list* raised \"unbound variable\" (a top-level REPL/script\nnever noticed, since core list* is already in scope there regardless\nof any SRFI-1 import).\n\nRe-added to (srfi s1 lists)'s own export list (it's a core global\nre-export, matching cons/car/cdr/list/etc's existing treatment) and\nto both shim files' export lists.\n\n* fix(srfi1): avoid non-tail cons-recursion in cons*/take/take-while/unfold\n\nIndependent security review found curry's per-function stack-overflow\nguard (the catchable \"call stack overflow\" condition) doesn't fire for\nnon-tail-recursive procedures defined inside a define-library body --\nthe process SIGSEGVs once the real C stack is exhausted instead. This\nis a pre-existing core interpreter gap (recorded separately for future\nwork), but this library made it trivially reachable at surprisingly\nlow thresholds for ordinary-sized input: (apply cons* (iota 800))\ncrashed at ~760 elements, (take (iota 1000000) 5000) crashed at ~5000,\nand unfold building a ~5000-element list crashed too -- far below what\nany real caller would consider \"very long\" (a several-thousand-row\nCSV/JSON array is enough to crash the whole VM with no diagnostic).\n\nRewrote all four as accumulator-based tail loops (reverse/\nappend-reverse at the end instead of building via cons in a non-tail\nposition). Verified live at sizes well past the old crash thresholds;\nregression tests added exercising 2000-20000 elements without needing\nto reproduce an actual crash in the test suite.\n\nNote: while auditing for the same import-order fix used elsewhere in\nthis branch, a fix to (srfi 69)/(srfi 125)'s own import order (to make\ntheir own hash-table-ref failure-thunk argument actually get invoked,\nmatching what this branch fixed for member/assoc/reduce) was found to\nhave a real collateral regression: (srfi s113 sets-and-bags) calls the\nCORE make-hash-table (integer cmp-mode) directly via its own bare\n(scheme base) import, relying on that name staying the original core\nprimitive in the shared flat GLOBAL_ENV -- the 69/125 fix makes their\nown override stick globally and permanently once imported anywhere in\nthe process, breaking srfi 113's internal usage (confirmed live:\nimporting (srfi 90) then (srfi 113) in sequence, as\ntests/akkadian_tests.scm does, then raises \"make-hash-table: only eq?,\neqv?, and equal? are supported\"). That fix is NOT included in this\nbranch -- reverted after finding the regression. This is a genuinely\ndeeper architectural issue (curry's SRFI shims share one flat\nGLOBAL_ENV with no isolation between \"this SRFI's override of a core\nname\" and unrelated code elsewhere that assumes core names stay put)\nthat needs a more careful fix than a blanket import-order swap.\n\nFull ctest suite: 95/95 pass, including akkadian_tests.scm (1738/1738,\nconfirmed unaffected by anything actually shipped in this commit).",
          "timestamp": "2026-08-14T19:57:20+10:00",
          "tree_id": "0bfbc90d9d27998709c12cd02bf6c69fd73d425e",
          "url": "https://github.com/deconstructo/curry/commit/9e7f5dd51c8134132997a96ec7149c7ee923d136"
        },
        "date": 1786701492100,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.413,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 32.047,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.129,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.933,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.648,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 385.085,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.476,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.42,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.864,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "eacf48fc3c179c010fdde2654ed689474f99f13a",
          "message": "feat(srfi279): add record-type-constructor/-predicate/-accessors/-mutators (#33)\n\n* feat(srfi279): add record-type-constructor/-predicate/-accessors/-mutators\n\nCloses the last SRFI-279 gap deliberately deferred from the original\nimplementation (see docs/reference/srfi/s279.md's own prior note):\nSRFI-279's rtd-properties wants the actual constructor/predicate/\naccessor/mutator procedure objects, not just their names, but curry's\nRecordType struct only stored name+field_names -- nothing referenced\nthe closures define-record-type's own codegen creates.\n\nExtends RecordType (src/object.h) with four new slots: constructor,\npredicate, and GC-visible accessors[]/mutators[] arrays (one entry per\nfield; mutators holds #f for a field declared immutable). The RTD is\nbuilt before any of its bindings' closures exist -- record_type_build_\nspec only describes what needs creating, see its own header comment --\nso these can't be populated eagerly at RTD-construction time the way\nname/field_names are. RecordBinding gained a role tag (constructor/\npredicate/accessor/mutator) and field_index so both define-record-type\ncodegen paths can stash each binding's closure back onto the RTD right\nafter it's created:\n\n- eval.c's tree-walker S_DEFINE_RECORD_TYPE case: builds closures\n  directly in C, so it just assigns the RTD's new fields inline.\n- compiler.c's native compile_define_record_type: closures don't exist\n  until the compiled bytecode actually runs, so it emits an extra\n  %rtd-set-constructor!/-predicate!/-accessor!/-mutator! call (new\n  internal primitives, src/builtins.c) right after each binding's own\n  compile_define, referencing the runtime RTD via the same rtd_ref\n  gensym'd shared variable the rest of this codegen already uses for\n  .scc-cache-safe identity (not the compile-time-only RecordType* that\n  record_type_build_spec itself builds -- that one only exists to\n  extract name/nfields/field_names for the emitted %make-record-type\n  call, never the object actually constructed at runtime).\n- %make-record-type (src/builtins.c) -- the runtime RTD constructor\n  compiler.c's codegen emits a call to -- needed the identical\n  constructor/predicate/accessors/mutators initialization added too.\n\nNew public primitives (record-type-* naming, matching this codebase's\nexisting record-type-name/-field-names convention):\nrecord-type-constructor, record-type-predicate, record-type-accessors\n(list, field order, always real procedures), record-type-mutators\n(list, field order, #f per immutable field). Wired into (srfi s279\ninspect)'s rtd-properties as rtd-constructor/-predicate/-accessors/\n-mutators.\n\nVerified across R6RS and R7RS record syntax, top-level and local\n(scope_depth > 0) define-record-type, and a .scc cache round-trip\n(fresh compile and cache-hit replay both reconstruct working\nprocedures correctly).\n\ntests/r7rs_tests.scm, tests/r6rs_tests.scm, tests/srfi_s279_inspect_\ntests.scm all extended. Full ctest suite: 95/95 pass.\n\n* fix(srfi279): address independent code+security review findings\n\nBoth a fresh code-review subagent and a fresh security-review\nsubagent (no shared context with the writing session) reviewed the\nRTD-accessors commit and each independently found and reproduced the\nsame critical bug:\n\n- CRITICAL: %rtd-set-constructor!/-predicate!/-accessor!/-mutator!\n  had no vis_rtd check on their first argument. These are ordinary\n  DEF'd globals like every other primitive in this file -- the %\n  prefix is a naming convention, not an access restriction curry's\n  flat global namespace can enforce -- so any Scheme script could call\n  them directly. vunptr blindly reinterpreted an arbitrary value's raw\n  bits as a RecordType* and wrote through it: (%rtd-set-accessor! 5 0\n  some-closure) and (%rtd-set-predicate! #f 'x) both segfaulted the\n  process (confirmed live by both reviewers independently). Fixed by\n  adding the same vis_rtd guard %record-ref/%record-set!/%record-pred?\n  already use for exactly this reason.\n- %rtd-set-accessor!/-mutator!'s field-index argument had no\n  vis_fixnum check either: a non-fixnum value's raw bits, reinterpreted\n  via vunfix's arithmetic right-shift, could silently alias into a\n  valid array slot -- (%rtd-set-accessor! rtd (integer->char 0)\n  hijack-closure) overwrote field 0's accessor instead of raising.\n  Fixed with the same vis_fixnum check tv_idx-style helpers elsewhere\n  in this codebase already use.\n- Negative-index handling was changed from uint32_t wraparound\n  (already safe by luck: a negative fixnum's raw bits truncate to a\n  LARGE unsigned value that correctly fails the nfields bounds check)\n  to an explicit signed comparison, for clarity rather than\n  correctness -- no behavior change, but no longer relying on\n  wraparound-happens-to-still-be-safe reasoning.\n\n- src/gc_gen.c's T_RECORD_TYPE scan_pinned_object case (the\n  experimental, non-default generational GC backend) was not updated\n  for the four new RecordType fields -- only rt->name and\n  field_names[] were evacuated. A minor GC promoting an object\n  referenced only via constructor/predicate/accessors[]/mutators[]\n  would leave a stale nursery pointer. Fixed to evacuate all four,\n  matching how T_ENV/T_CLOSURE/etc in the same function stay\n  exhaustive for their own pointer fields.\n\nRegression tests added for the type-confusion and index-aliasing\nfixes; full ctest suite: 95/95 pass.",
          "timestamp": "2026-08-15T06:51:30+10:00",
          "tree_id": "17cca2cb77809d472b61fce2175a37fefaa8eb93",
          "url": "https://github.com/deconstructo/curry/commit/eacf48fc3c179c010fdde2654ed689474f99f13a"
        },
        "date": 1786740734261,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 24.173,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.698,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.307,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.33,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 210.456,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 374.582,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.806,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 124.949,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 106.708,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "f79e2adb56b7a96d7fa05a845273e8eee97fbefe",
          "message": "docs: fix dead PHILOSOPHY.md link, update stale SRFI/module counts (#34)\n\ndocs/PHILOSOPHY.md was deliberately deleted in a834cc5b (\"removed\")\nbut README's link to it was never cleaned up -- a real 404. Removed\nthe link, keeping the surrounding sentence as plain prose.\n\nThe SRFI and module counts had also drifted: README claimed 32 SRFI\nlibraries (docs/reference/srfi/index.md now lists 37) and ~35 modules\n(docs/reference/modules.md now lists 56) -- both stale from before\nthis session's SRFI-4/160/279 and other additions.",
          "timestamp": "2026-08-15T07:11:12+10:00",
          "tree_id": "644c807bed2a83dc7d1d6f8a5ad8c5e42bf009ff",
          "url": "https://github.com/deconstructo/curry/commit/f79e2adb56b7a96d7fa05a845273e8eee97fbefe"
        },
        "date": 1786741913022,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 27.081,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 32.019,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 7.414,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.657,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 268.656,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 409.714,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 75.44,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 147.618,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 129.469,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "b8f0e786135ae70620bda3a32413852a9c678c80",
          "message": "feat(srfi253): add Data (Type-)Checking; add bignum?/multivector? (#35)\n\nImplements SRFI-253: check-arg, values-checked, check-case,\nlambda-checked, case-lambda-checked, define-checked, define-record-\ntype-checked -- available as (srfi 253), (srfi srfi-253), and\n(srfi s253 data-checking), matching this codebase's existing SRFI\nnaming convention.\n\nAny ordinary unary predicate works as a checker, so curry's extended\ntype system (bignum?, rational?, complex?, quaternion?, octonion?,\nmultivector?, surreal?, symbolic?, quantum?, matrix?, tensor?,\nspinor?, actor?) already works with lambda-checked/define-checked/etc\nfor free -- except bignum? and multivector? didn't exist as core\npredicates at all before this commit (every other extended-tower/\nobject predicate did). Added both (src/builtins.c) so this SRFI can\nactually be used meaningfully across curry's whole type system, not\njust R7RS's base types.\n\ncurry has no native case-lambda at all (not even as a core special\nform, and no SRFI-16 shim exists either) -- case-lambda-checked is\nself-contained: a single variadic (lambda args ...) that dispatches\non the call's actual argument count by hand, rather than expanding\ninto calls to a case-lambda primitive that doesn't exist.\n\nTwo real macro-expansion bugs found and fixed during implementation\n(both caught by direct testing before committing, not by a formal\nreview round):\n- Every internal helper macro an exported macro's expansion reaches\n  (%values-checked, %lambda-checked, %clc-dispatch/-try, the\n  define-record-type-checked helpers) had to be exported too, even\n  though none is meant for direct external use -- curry's syntax-rules\n  is not hygienic across define-library boundaries (see\n  docs/reference/writing-a-module.md's own documented gotcha); omitting\n  them raised \"unbound variable\" the first time a user actually called\n  the exported macro, not at import time.\n- define-record-type-checked originally spliced an unexpanded\n  (%drtc-raw-field field-spec) macro call directly into\n  define-record-type's own field-spec position. define-record-type is\n  compiled directly (src/compiler.c's dedicated\n  compile_define_record_type), not itself a macro that expands its\n  own arguments first, so it read that call completely literally --\n  silently producing a garbled record type whose real accessors were\n  never actually defined. Fixed by pre-expanding the whole field-spec\n  list into plain (fname acc [mod]) forms via a CPS-style helper\n  BEFORE splicing anything into define-record-type, rather than\n  relying on define-record-type to macro-expand a nested call itself.\n\nCurry-specific scoping decision for define-record-type-checked:\nconstructor argument names must appear in the same order as the\nfield-specs they're checked against (matched positionally, not by\nname) -- simpler to implement correctly in portable syntax-rules (no\ncompile-time identifier-equality machinery, which syntax-rules can't\ndo portably at all) and matches how every existing define-record-type\ncall in this codebase is already written.\n\ntests/srfi_253_tests.scm: 42 checks covering all seven forms plus\ncurry's extended-type predicates as checkers. tests/r7rs_tests.scm:\n4 new checks for bignum?/multivector? directly. Full ctest suite:\n96/96 pass.",
          "timestamp": "2026-08-15T08:05:10+10:00",
          "tree_id": "9072738d660cdf39065b00623d51cb0875d0379f",
          "url": "https://github.com/deconstructo/curry/commit/b8f0e786135ae70620bda3a32413852a9c678c80"
        },
        "date": 1786745145066,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.555,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.896,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.578,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.649,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 180.831,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 321.774,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.934,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 102.739,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 94.744,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "e86753efc527686252675fae933637bd07f5007d",
          "message": "fix(srfi253): fix exponential case-lambda-checked expansion blowup (#36)\n\nIndependent security review found and measured a real macro-\nexpansion-time resource-exhaustion bug: %clc-dispatch spliced the\nnext clause's full unexpanded dispatch form in directly at every\narity-mismatch branch inside %clc-try's own recursion, making\nexpansion cost genuinely EXPONENTIAL in clause count (base ~= 2p+1\nfor p parameters per clause). Measured: 4x4 clauses/params -> 0.11s,\n5x5 -> 2.48s, 6x5 -> 27.7s, 8x4 -> didn't finish compiling in over 2\nminutes -- entirely reasonable-looking user code, reachable from any\ncode path compiling less-than-fully-trusted Curry source (an MCP\ntool, a REPL-as-a-service).\n\nFixed by wrapping the \"try the next clause\" continuation in a thunk,\ncreated once per clause via let, so only the resulting tiny 2-token\ncall gets duplicated across %clc-try's several branches -- not the\nwhole recursive dispatch tree. Expansion cost is now linear in\n(clauses x parameters) again; verified the same 8x4 stress case now\ncompiles and runs in ~12ms, not 2+ minutes. Regression test added\nexercising the exact shape that used to blow up.\n\nAlso documents (in both the code comment and docs/reference/srfi/\ns253.md) a second, narrower finding from the same review round:\ndefine-record-type-checked has a real, if short-lived, window between\nbinding the raw unchecked constructor/modifiers under their public\nnames and shadowing them with checked wrappers, during which another\nalready-running actor calling the constructor/a modifier by name gets\nthe unchecked version. Not fixed with a redesign -- doing so would\nneed generating a distinct fresh identifier per mutable field within\none define-record-type call, which portable syntax-rules genuinely\ncannot do (no gensym, no identifier concatenation) -- documented as a\nknown limitation instead, with the concrete usage guidance (define\ntypes before spawning actors that use them) that avoids it.\n\ntests/srfi_253_tests.scm: 43 checks (up from 42). Full ctest suite:\n96/96 pass.",
          "timestamp": "2026-08-15T08:20:19+10:00",
          "tree_id": "a5ac4ffb076b8959da10cec36ca02b2aa134ae2c",
          "url": "https://github.com/deconstructo/curry/commit/e86753efc527686252675fae933637bd07f5007d"
        },
        "date": 1786746055867,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.909,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.983,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.599,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.935,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 183.88,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 327.585,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 68.629,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 105.414,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 93.095,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "5b24bfda0b7cdb6164e0675bc92bda076a2e0e69",
          "message": "docs(thoughts): FLINT integration design doc (#37)\n\nNot implemented -- a scoping/design pass before any code, per explicit\nrequest. Grounds the proposal in what curry actually already has\n(MPFR arbitrary-precision floats and a substantial always-on number\ntheory library are both already implemented and documented, contrary\nto what \"MPFR you say?\" might suggest was still just a roadmap item;\nthe CAS already has polynomial GCD/factorization too, just via naive/\nsubresultant algorithms rather than FLINT's FFT/modular-accelerated\nones), then covers what FLINT 3.x actually is (post-Arb/Antic/Calcium\nmerger: certified ball arithmetic, algebraic number fields, exact\nalgebraic+transcendental computation, on top of its long-standing\nfast bignum/polynomial/matrix/finite-field core), three named\ncandidate integration architectures with explicit tradeoffs (isolated\nmodule vs deep numeric-tower integration vs a staged approach --\nrecommending the staged one), a proposed phase-1 scope, and five open\nquestions flagged for explicit decision rather than silently picked\n(module scope, BUILD_FLINT/BUILD_MPFR CMake option relationship,\nnaming, license compatibility -- checked clean, GPL-3.0-or-later\ncurry linking LGPL-3.0-or-later FLINT is standard -- and whether a\nfirst pass should go through (curry ffi) directly rather than a\nhand-written C module).",
          "timestamp": "2026-08-15T08:25:12+10:00",
          "tree_id": "f59085db8eb730f8f623aab9f931b03c9c54bf62",
          "url": "https://github.com/deconstructo/curry/commit/5b24bfda0b7cdb6164e0675bc92bda076a2e0e69"
        },
        "date": 1786746366402,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.211,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.235,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.972,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.539,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 190.104,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 368.04,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.33,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.609,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.469,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "1831adec23af69b436366568527d9caf8a733d5a",
          "message": "fix(core): make write/display cycle-safe on circular structure (#39)\n\n* fix(core): make write/display cycle-safe on circular structure\n\nR7RS 6.13.3 requires write/display to not loop forever on circular data (only write-simple is exempt). write-shared already implemented correct #n=/#n# datum-labeling via a two-pass ref-counting walk (ws_count_refs/ws_write); write/display used a separate, naive recursive path with no cycle tracking and hung forever on a circular pair or vector.\n\nRoute write/display through that same cycle-safe machinery (adding scm_display_shared, an as_display-parameterized variant of ws_write) instead of writing new detection logic. write-simple keeps calling the original naive scm_write directly, matching its documented R7RS license to loop on cycles.\n\nCloses the KNOWN LIMITATION gap called out in srfi/s279's inspect-properties and referenced from srfi/s1's circular-list.\n\n* fix(core): close remaining hang/crash paths found by independent review\n\nCode review found that only the write/display Scheme primitives were routed through the cycle-safe writer -- several internal C call sites (REPL result echo, uncaught-exception printers, (error ...) with a non-string message, actor-crash reporting, the trace facility) still called the raw, cycle-unsafe scm_write/scm_display directly, so a circular value could still hang the process without ever calling write/display explicitly. Switched those call sites to scm_write_shared/scm_display_shared.\n\nSecurity review then found that this exposed a much easier-to-trigger regression: ws_count_refs (the first pass of the cycle-safe writer) recursed on both car and cdr for every cons cell, so an ordinary long flat list -- no cycle, no sharing -- now overflowed the C stack via plain write()/display(), at as few as ~150-200k elements, since those primitives route through this pass unconditionally. Fixed by making the cdr walk iterative (only car recursion remains, bounded by nesting depth rather than list length), matching the shape ws_write_list already used for its own second-pass cdr walk. Verified with a 2M-element flat list.\n\nAlso skip the WSharedMap allocation entirely for non-compound (atomic) values in scm_write_shared/scm_display_shared, avoiding an unconditional map alloc on every write/display call now that they're the hot path rather than the rarely-used write-shared.",
          "timestamp": "2026-08-15T12:15:28+10:00",
          "tree_id": "53aa3e9f7c53dbcef8b4de9233cef86f5cb45cf0",
          "url": "https://github.com/deconstructo/curry/commit/1831adec23af69b436366568527d9caf8a733d5a"
        },
        "date": 1786760180948,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 22.255,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.454,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.132,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.841,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 205.119,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 333.403,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 69.76,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.074,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 105.11,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "e0ccdd8b09778eb5baabe6df1cc8f6ef614c5c51",
          "message": "docs(srfi279): fix stale rtd-accessors header comment (#38)\n\nlib/curry/modules/srfi/s279/inspect.scm's own header comment still\nlisted rtd-accessors/-mutators/-predicate/-constructor under\n\"Deliberately deferred, not forgotten\" -- PR #33 implemented and shipped\nthese, and updated docs/reference/srfi/s279.md's own \"Deliberately out\nof scope\" list accordingly, but missed this file's own header comment\n(caught by direct user review of the source). Removed the stale\ndeferred-item bullet and added it to the \"As of this version\" summary\nparagraph alongside the other already-shipped additions.",
          "timestamp": "2026-08-15T12:22:37+10:00",
          "tree_id": "97f53d2a228fdd97e5387e03b1c48ab35b188b51",
          "url": "https://github.com/deconstructo/curry/commit/e0ccdd8b09778eb5baabe6df1cc8f6ef614c5c51"
        },
        "date": 1786760602483,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.981,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.615,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 7.223,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.881,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 203.491,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 342.233,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.06,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 131.705,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 103.41,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "aec3b8a0b4d763fa318af801622ca8acb1c6f97a",
          "message": "fix(tests): remove grep/printf subprocess forking from lsp test assertions (#42)\n\nReproduced the intermittent 'lsp' ctest failure (issue #40) locally by running 20-30 parallel test_lsp.sh instances under artificial CPU load. The failures were not data corruption: re-grepping a failed run's own saved log afterward found the exact substring check_contains claimed was missing, present byte-for-byte. Root cause is check_contains/check_not_contains forking a printf+grep pipeline for every single assertion (dozens per run) -- under process/fork pressure that pipeline can fail to spawn or return a misleading exit status unrelated to the actual haystack content.\n\nReplaced with bash's builtin [[ == *needle* ]] substring test, which does the same literal match with zero process creation. Confirmed: 8/20 failures under load before the fix, 0/20 and then 0/30 after, across two separate load runs. Full ctest suite (96/96) still passes.",
          "timestamp": "2026-08-15T12:36:48+10:00",
          "tree_id": "f9afc7afd56f334bd2318bf4f12c853390bac448",
          "url": "https://github.com/deconstructo/curry/commit/aec3b8a0b4d763fa318af801622ca8acb1c6f97a"
        },
        "date": 1786761446974,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.726,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 19.279,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.752,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 23.483,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 159.043,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 260.711,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 54.024,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 92.585,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 82.549,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "34b75df191ea28111bbee64d05fe32656abc8998",
          "message": "docs(akkadian): fix wrong map glyph, add missing number->string row (#43)\n\nIssue #6: the reference documented map as 𒈷𒌝 (ME.UM), but src/akkadian_names.h actually binds that glyph to number->string; map's real glyph is 𒈷𒅆 (ME.IGI). Following the doc for (𒈷𒌝 ...) silently ran number->string instead of map, with no error to reveal the mistake since both glyphs resolve to real (but different) procedures. Verified against src/akkadian_names.h and by evaluating both glyphs directly.\n\nFixed map's row to the correct glyph, and added the previously-missing number->string row so the collision would have been visible in the doc's own table. Also bumped the stale v1.2.2 stamp to the current 1.20.0.",
          "timestamp": "2026-08-15T12:47:47+10:00",
          "tree_id": "436c68705c3d77e062f81114f5e98c4226967d0f",
          "url": "https://github.com/deconstructo/curry/commit/34b75df191ea28111bbee64d05fe32656abc8998"
        },
        "date": 1786762124318,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.86,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.062,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.945,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.015,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 188.803,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 369.68,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.848,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.281,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.61,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "bdbd5cf11bf3fadae12fa74610e5aadf1cffba59",
          "message": "docs(thoughts): solve/variable-isolation/elimination design doc (#44)\n\nMotivated by issue #10 (dharmatech): interactive equation-solving/variable-isolation demo, plus a follow-on question about a Jupyter-lite-style UI on curry. This doc scopes just the CAS primitives (solve/isolate/eliminate) that such a UI would eventually call -- the UI itself is explicitly deferred by the repo owner pending objects/packages/module support and is out of scope here.\n\nChecked against src/symbolic.h/symbolic.c/sx_poly.c and docs/reference/symbolic.md: curry's CAS has extensive diff/integrate/simplify/collect/degree machinery and an existing sx_lt/le/gt/ge comparison-node pattern to mirror, but no equation representation and no solve/isolate/eliminate at all -- this is new surface, not an extension of something partial.\n\nProposes three tiers (isolate: single-occurrence inverse-operation peeling, directly matching the demoed UI's click-to-isolate interaction; solve: closed-form linear/quadratic via existing sx_collect/degree; eliminate: pairwise substitution across a small system, built entirely from isolate/solve plus the already-existing sx_substitute), an equation-representation recommendation (a new SX_EQ operator mirroring the existing SX_LT/LE/GT/GE pattern, not a separate record type), and flags the one real user-facing design choice left open: how multi-valued inverses (x^2=4 has two roots) should be returned. Design doc only, not yet implemented.",
          "timestamp": "2026-08-15T13:40:45+10:00",
          "tree_id": "6fcc0f49749d68b9729b9e6f9ff6dee407fceed2",
          "url": "https://github.com/deconstructo/curry/commit/bdbd5cf11bf3fadae12fa74610e5aadf1cffba59"
        },
        "date": 1786765289041,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 22.384,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.193,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.012,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.264,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 188.881,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 369.746,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.341,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.277,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.856,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "5770b175806ad7d15d3cf2634a0060849b35ddec",
          "message": "fix(core): string->number(\"\") returns #f, not 0; utf8->string validates input (#45)\n\n* fix(core): string->number(\"\") returns #f, not 0; utf8->string validates input\n\nTwo core bugs previously documented as known limitations in (srfi 279)'s docs (which explicitly worked around both rather than fixing them, since they're outside that module's own scope).\n\nstring->number: parse_number's fixnum-range fast path used 'errno == 0 && *end == 0' as its success check. strtol(\"\", &end, radix) consumes nothing (end == s), returns 0, and leaves errno untouched -- indistinguishable from a genuine \"0\" by that check alone, so (string->number \"\") silently returned 0 instead of #f (R7RS: the empty string is never a valid number literal). Fixed with an explicit empty-string guard at the top of parse_number, which also correctly covers empty numerator/denominator/real/imaginary substrings reached via the function's own recursive calls (e.g. \"3/\").\n\nutf8->string: did a raw memcpy from the bytevector into a new String with zero UTF-8 validation -- any bytevector, however malformed (truncated sequences, stray continuation bytes, overlong encodings, encoded UTF-16 surrogate halves, lead bytes past the 4-byte range), silently became a String with garbled content. Added a validator mirroring string-ref's existing lead/continuation decode shape, raising wrong-type-argument on malformed input instead.\n\nUpdated (srfi 279)'s inspect.scm to match: the string->number empty-string guard is now redundant (string->number itself returns #f) and removed; the bytevector utf8->string entry is now wrapped in guard since it can raise, omitting the entry on invalid UTF-8 rather than propagating the exception through the whole inspect-properties call -- matching the module's own \"omit unsupported property\" convention. Updated docs/reference/srfi/s279.md accordingly.\n\nAdded regression tests for both fixes plus edge cases (overlong encoding, stray continuation byte, surrogate half, lead byte past 4-byte range) to tests/r7rs_tests.scm.\n\n* fix(core): close integer-overflow OOB read in utf8_is_well_formed\n\nIndependent security review (fresh context, per CLAUDE.md) on the previous commit found: utf8_is_well_formed's truncation check 'i + seqlen > len' used uint32_t arithmetic that can wrap around for a bytevector near UINT32_MAX in length (make-bytevector's own documented upper bound) -- a crafted trailing lead byte near the end of such a bytevector would make i + seqlen wrap past UINT32_MAX back down to a small value, spuriously pass the truncation check, and read up to 3 bytes past the bytevector's allocation.\n\nFixed by comparing 'seqlen > len - i' instead: i < len is the loop's own invariant, so len - i can never underflow, making this comparison overflow-safe for any i/len/seqlen rather than needing a wider integer type.\n\nAlso fixed a now-stale comment in prim_string_ref (found by the same review pass) that still described utf8->string as never validating its input.",
          "timestamp": "2026-08-15T13:52:49+10:00",
          "tree_id": "17793f503a13c21272628b1bdf983ef6d8de0938",
          "url": "https://github.com/deconstructo/curry/commit/5770b175806ad7d15d3cf2634a0060849b35ddec"
        },
        "date": 1786766018920,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.545,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.51,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.175,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.918,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.133,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 368.927,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.88,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.847,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.968,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "c2c39d3b821684d4cb87c057d2eeba6de0674cf7",
          "message": "perf(srfi279): move indexed-pairs construction to C, ~10x faster on large sequences (#47)\n\ninspect-properties' documented Known Limitation (roughly 45x slower / 5x more memory than the underlying accessor on large vectors/bytevectors) was profiled to find the actual dominant cost: %indexed-properties, the loop building the (index element) alist for every pair/string/vector/bytevector/typedvec property set. Measured on a 2M-element vector, inspect-properties took ~4.8s against ~0.06s for vector->list alone; instrumenting %vector-properties directly attributed ~4.5s of that (over 90%) to %indexed-properties specifically, not the redundant list re-materialization fixed in an earlier commit (that accounted for only a few percent).\n\nRoot cause: the same loop logic run at curry's top level (outside any define-library body) took ~1.25s for the identical input, a ~3.6x difference matching this codebase's own previously-documented 'define-library hot loops ~3x slower than top-level' interpreter characteristic -- not something specific to this module.\n\nAdded %indexed-pairs, a small C primitive (src/builtins.c) doing the same O(n) walk natively: two passes over the input list (count, then fill a GC-scanned scratch array of the (i car) sub-pairs), building the result list from the end backward to avoid both an O(n) Scheme-style reverse and unbounded C recursion depth. inspect.scm's %indexed-properties now delegates to it directly.\n\nMeasured improvement: the same 2M-element vector's inspect-properties call dropped from ~4.8s to ~0.44s (~11x). At the doc's original 50M-element-scale measurement (u8vector), the ratio to the underlying accessor drops from ~45x to ~5x (measured freshly at 10M-element scale: ~0.4s accessor vs ~2.1s inspect-properties).\n\nAdded direct regression tests for %indexed-pairs (empty list, single element, order preservation, non-list/dotted-pair inputs, and a 10k-element scale round-trip), and updated docs/reference/srfi/s279.md's Known Limitations entry with the new measurements.",
          "timestamp": "2026-08-15T14:12:31+10:00",
          "tree_id": "8c97fb94563fada4dccbc4ee22b278895234ffe8",
          "url": "https://github.com/deconstructo/curry/commit/c2c39d3b821684d4cb87c057d2eeba6de0674cf7"
        },
        "date": 1786767197747,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.498,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 35.45,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.346,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 40.589,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 193.62,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 375.32,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.454,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 122.593,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 103.468,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "bc11de06a43d85c6a281e2a834e90853ef77c8cd",
          "message": "fix(core): close circular-list hang in %indexed-pairs (independent security review) (#48)\n\nUrgent follow-up to #47, which merged before this fix (found by an independent security review that was still running when #47's merge happened) landed. main currently has the live bug: %indexed-pairs' counting pass was a bare vis_pair walk with no cycle detection -- reachable from ordinary unprivileged Scheme (set-cdr! alone builds a circular list) since it's an ordinary globally-registered primitive in curry's flat GLOBAL_ENV, not gated behind inspect.scm's own list?-before-call convention the way its only current caller happens to use it. A circular list hangs the process forever in a tight, uninterruptible C loop.\n\nFixed with the same Floyd tortoise-and-hare pattern prim_length already uses elsewhere in this file. As a side effect this also makes a dotted/improper-list argument raise instead of silently truncating at the first non-pair cdr and returning a partial result -- a second, lower-severity issue the same review found, evidenced by a test that was asserting the silent-truncation behavior while its own label claimed the opposite ('raises on a dotted list' checking a non-raising equality assertion). Both tests corrected to actually guard/expect a raise.\n\nAlso added a defense-in-depth i < n bound on the fill loop: pass 1 fixes the trip count, but nothing enforces that lst can't be mutated by another thread between the two passes for some future caller that doesn't share inspect.scm's own guarantee of a fresh, unshared list.\n\nVerified: the circular-list repro now raises immediately instead of hanging; full ctest suite (96/96) passes; the 2M-element-vector inspect-properties benchmark from #47 is unaffected (~0.46s -- Floyd's algorithm's overhead is negligible against the already-necessary single list walk).",
          "timestamp": "2026-08-15T14:35:38+10:00",
          "tree_id": "12abaee2d748917d4619279915edb11d3d5a9d46",
          "url": "https://github.com/deconstructo/curry/commit/bc11de06a43d85c6a281e2a834e90853ef77c8cd"
        },
        "date": 1786768584848,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.593,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.53,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.964,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.872,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 191.96,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 372.074,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.524,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 119.466,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.11,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "a56679fd6fcadfcbf2fde07f52be31e1b7f4b6fd",
          "message": "feat(core): add R7RS string->vector/vector->string (#49)\n\nCloses #46. Neither was implemented at all -- (srfi 279)'s inspect.scm had its own local list->vector/list->string-composed workaround with a header comment noting the gap, but nothing else in the codebase provided them.\n\nstring->vector does the same single-pass UTF-8 codepoint decode string->list already does (byte range -> variable-width codepoints, final element count not knowable upfront), but builds straight into a Vector instead of going through an intermediate list, filling backward once the count is known (same shape %indexed-pairs already uses elsewhere in this file).\n\nvector->string explicitly validates every element in range is a character and raises otherwise, rather than the lax vunchr-whatever's-there pattern list->string already uses -- new code, no existing lax behavior to preserve compatibility with.\n\ninspect.scm's own %string-properties/%vector-properties are left as-is: they already reuse an already-materialized list via plain list->vector/list->string rather than calling these new primitives, which would be strictly less efficient (re-deriving the list from the original string/vector a second time).",
          "timestamp": "2026-08-15T14:57:54+10:00",
          "tree_id": "5924f2c408254f457479b5b8816873ee2093e497",
          "url": "https://github.com/deconstructo/curry/commit/a56679fd6fcadfcbf2fde07f52be31e1b7f4b6fd"
        },
        "date": 1786769920868,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 12.016,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 15.474,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.315,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 18.206,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 97.31,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 181.156,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 44.168,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 62.417,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 50.569,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "926579b7dee8dad9345ada9acf2a4300c0d8f22c",
          "message": "fix(core): eval() stack-depth guard; compiler support for delay/delay-force (#50)\n\n* fix(core): eval() stack-depth guard; compiler support for delay/delay-force\n\nInvestigating #2 (define-library non-tail recursion SIGSEGVs) and #3 (define-library hot loops ~3.6x slower than top level) found both share one root cause: define-library bodies are tree-walked (modules.c's define_library_clause calls eval() directly), not compiled+VM-run the way top-level script/REPL code already is (main.c: compiler_compile()+vm_run()). The VM has an explicit, catchable call-stack-overflow guard (vm.c, max 256 frames); eval() had none at all, so deep non-tail recursion just exhausted the real OS stack and crashed.\n\n#3's actual fix -- switching modules.c to compile+vm_run -- turned out to be architecturally blocked: the VM's global-variable ops are hardwired to one GLOBAL_ENV (vm.c's own header comment says so directly), while define-library bodies execute against an isolated root environment specifically so library-internal defines don't leak into the global namespace. A naive swap would silently break module isolation for every existing module. This exact problem, and a proposed fix (repair modules.c's currently-broken export-list filtering and make that the real isolation mechanism instead of environment-frame topology), turns out to already be scoped in docs/thoughts/eval-elimination-migration-plan-2026-07-23.md -- an existing 8-phase plan whose phase 1 (splitting eval.c into runtime.c + a shrinking tree-walker) is already merged. Phases 2-8, including the modules.c rewrite that would actually close #3, are a separate, larger, already-planned undertaking -- not attempted here.\n\n#2 is fixed on its own: eval() now carries a stack-depth guard (a per-thread cached stack base, from Boehm's GC_get_stack_base, compared against the current stack pointer on every real C-level entry -- goto-tail iterations don't grow the C stack and never reach the check), raising the same catchable EC_STACK_OVERFLOW condition the VM's own guard uses instead of segfaulting. Verified: a define-library-defined function doing non-tail recursion to depth 1,000,000 previously crashed the process immediately; now raises cleanly and is catchable via guard. The new threshold (~1400-1600 non-tail frames with the current 7MB budget) is well above the VM's own established 256-frame limit for compiled code, so no new false-positive risk relative to already-accepted behavior.\n\nWhile checking compiler/tree-walker parity before touching modules.c (a prerequisite investigation, not itself part of the swap), found delay/delay-force are unbound in compiled code today -- eval.c has always specially handled them, but compiler.c never learned to, so top-level (define x (delay ...)) has apparently always raised unbound-variable. Fixed with actual new compiler codegen (compile_delay in compiler.c, reusing compile_lambda for the thunk, calling two new primitives %delay-promise/%delay-force-promise in builtins.c that build the same Promise shape eval.c's own S_DELAY/S_DELAY_FORCE construct) rather than eval.c's tree-walker-specific Closure construction -- verified force/delay/delay-force all now work identically whether compiled or tree-walked, and prim_force's existing apply(p->val, V_NIL) is already closure-representation-agnostic so no changes were needed there. No existing module in the codebase used delay/delay-force/defined? inside a define-library body (grep confirmed), so this closes a real, independently-shippable bug rather than working around a live regression.\n\nNew ctest entry define_library_stack_guard (own file, not folded into r7rs_tests.scm, matching this suite's convention for scenarios that would previously crash the whole test binary rather than just fail one assertion).\n\n* fix(core): eval() stack guard must query real per-thread stack size\n\nIndependent security review of the previous commit found the guard's fixed 7MB threshold assumed every thread has an ~8MB stack, matching the main thread and actors.c's own actor threads -- but curry's parallel map/reduce/for-each/par worker pool (workpool.c) spawned its threads with a NULL pthread_attr_t (platform default stack size, 512KB on macOS), so the fixed 7MB threshold never fired there: the real, much smaller stack still exhausted and crashed the process (confirmed: Bus error at non-tail recursion depth ~150 through a define-library-defined function inside a parallel map, reproduced with no special configuration -- an ordinary default build/run). The same root cause also reproduced on the main thread under a lowered ulimit -s (confirmed below ~7.2MB).\n\nFixed two ways:\n1. eval()'s guard now queries each thread's real stack size (pthread_get_stacksize_np on macOS, pthread_getattr_np+pthread_attr_getstack on Linux) instead of assuming a fixed 8MB, reserving 1/8 of the real size (minimum 64KB) as unwind headroom -- scales correctly for both a normal ~8MB thread and workpool's previous 512KB default.\n2. workpool.c now explicitly requests an 8MB stack via pthread_attr_setstacksize, matching actors.c's existing convention, so worker threads get the same headroom as every other thread in the process rather than being uniquely constrained.\n\nVerified: the exact crash repro from the security review (parallel map over a define-library-defined non-tail-recursive function) now completes/raises cleanly instead of crashing; the ulimit -s 7168 repro now raises and is caught instead of segfaulting. Added two new regression tests exercising workpool worker threads specifically (map auto-parallelizes above the default 8-element threshold, so a 12-element list genuinely exercises worker threads, not just the calling thread) -- the existing tests only covered the main thread and wouldn't have caught this.",
          "timestamp": "2026-08-15T18:42:16+10:00",
          "tree_id": "edf4293535b3d90647cfa9ffd116a56a2fa480e7",
          "url": "https://github.com/deconstructo/curry/commit/926579b7dee8dad9345ada9acf2a4300c0d8f22c"
        },
        "date": 1786783383435,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.899,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.322,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.012,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.998,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 190.505,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 367.697,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.016,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.313,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 100.682,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "7eac86cedea22a58b2fafda1a744353eef571959",
          "message": "Release v1.21.0 (#53)\n\nCHANGELOG.md documents everything merged since v1.20.0: the write/display cycle-detection fix, string->number/utf8->string validation fixes, string->vector/vector->string, delay/delay-force compiler support, the eval() stack-depth guard (plus the worker-pool thread-stack-size follow-up), the SRFI-279 inspect-properties perf fix (and the circular-list DoS found alongside it), SRFI-253, SRFI-1 completion, SRFI-4/160, further SRFI-279 gap closures, the lsp test flake fix, and the Akkadian map-glyph doc fix.\n\nsrc/version.h is CMakeLists.txt's single source of truth for the project version. Formula/curry.rb's url/version point at the not-yet-created v1.21.0 tag; sha256 stays at the v1.20.0 tarball's checksum until the tag exists and can be hashed for real, in a follow-up commit after tagging (matching this repo's own established two-step pattern).\n\nVerified: curry -v reports 1.21.0, full ctest suite (97/97) passes.",
          "timestamp": "2026-08-15T19:13:16+10:00",
          "tree_id": "b2d5319131b110a73db9bcbdcc5f35ab4b4ddeaf",
          "url": "https://github.com/deconstructo/curry/commit/7eac86cedea22a58b2fafda1a744353eef571959"
        },
        "date": 1786785233580,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.005,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.936,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.025,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.748,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 193.685,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 370.746,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.672,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.649,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.593,
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
          "id": "c503d27f0449fd108fc0bc9474aa948ff656cd3a",
          "message": "fix(homebrew): correct v1.21.0 tarball sha256 in Formula/curry.rb\n\nThe v1.21.0 release commit (7eac86c) updated url/version but left the\nstale v1.20.0 sha256 in place, breaking `brew install`.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-15T19:26:58+10:00",
          "tree_id": "f5df4d2e11443d6a2b0fc6bb51576f3e185f22cf",
          "url": "https://github.com/deconstructo/curry/commit/c503d27f0449fd108fc0bc9474aa948ff656cd3a"
        },
        "date": 1786786080424,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.255,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 35.002,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.041,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.932,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.343,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 368.434,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.671,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.176,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.733,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "7aecf3ff64bab2740468e62fa47e594e06909a44",
          "message": "docs(thoughts): draft SRFI-279 upstream contribution (curry.scm port) (#52)\n\nNot yet submitted -- see the README for why (SRFI-279 is still draft status, upstream's own README funnels participation through its mailing list rather than GitHub PRs/issues, and the existing per-implementation files in that repo all carry the SRFI author's own copyright with no established third-party-PR precedent to point to).\n\ncurry.scm is a working port of lib/curry/modules/srfi/s279/inspect.scm into the flat include-able shape the upstream repo's chibi.scm/guile.scm/kawa.scm already use -- verified end-to-end against a live curry build (built a temporary define-library, included curry.scm with the imports 279.sld.diff proposes, confirmed correct inspect-properties/inspect-describe output across numbers, pairs, strings, vectors, typed numeric vectors including the separate f64vector module, bytevectors, hash tables, boxes, char-sets, records, record-types, and procedures). Two required imports (srfi 1 for last-pair/every, curry f64vector) weren't obvious from curry's own module and were only found by actually running it against this exact include shape.\n\n279.sld.diff is the corresponding cond-expand clause addition wiring curry's own 'curry feature identifier to these imports and the new file.",
          "timestamp": "2026-08-15T19:30:23+10:00",
          "tree_id": "9f0fc22392097f4ad06841a6df3cacd6a6e7a443",
          "url": "https://github.com/deconstructo/curry/commit/7aecf3ff64bab2740468e62fa47e594e06909a44"
        },
        "date": 1786786283289,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 25.222,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.168,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 7.573,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 40.27,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 218.71,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 383.241,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 73.91,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 134.857,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 111.478,
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
          "id": "3f2e12a643925f45b33bd3bcc62345805068267d",
          "message": "docs(srfi279): document universal id/size/type properties\n\ninspect-properties has always prefixed every object alist with id\nand appended size/type when inferrable, but the doc only mentioned\nwrite/display, leaving the real output shape undocumented.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-15T20:39:33+10:00",
          "tree_id": "79e3509e77d843d1b57fd70fd125379cd98c8081",
          "url": "https://github.com/deconstructo/curry/commit/3f2e12a643925f45b33bd3bcc62345805068267d"
        },
        "date": 1786790431006,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.256,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 24.654,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.791,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.61,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 159.183,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 288.349,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 53.759,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 92.869,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 81.129,
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
          "id": "86d59164d1d0089f223c6b2bf420f21c612e53e0",
          "message": "feat(s3): pure-Scheme (curry s3) module, replacing the C-level S3 client\n\nNew (curry s3) (lib/curry/modules/curry/s3.scm): AWS SigV4-signed S3\nclient built entirely on existing curry building blocks (crypto, http,\nxml, srfi 19/132) -- no new C module. Covers put/get/delete/head/copy,\nbucket listing (ListObjectsV2), bucket create (with the region-specific\nLocationConstraint body AWS requires)/delete, presigned URLs, and\nmultipart upload with automatic part splitting. Also works against any\nS3-compatible endpoint: R2, MinIO, Ceph, GCS-via-S3-interop, Wasabi.\n\nBoth signing flavors (Authorization-header and query-string/presign)\nwere cross-checked byte-for-byte against an independent Python\nreference implementation during development.\n\nRemoved S3 support from the C (curry storage) module entirely (Swift\nand Azure unchanged), along with its now-dead Akkadian aliases.\n\nAlong the way, fixed two real bugs in (curry http):\n- http-request/-headers sized string request bodies via strlen instead\n  of the length-aware curry_string_length, silently truncating at the\n  first embedded NUL byte.\n- HEAD requests never set CURLOPT_NOBODY, relying on the server to\n  volunteer no body instead of actually requesting one.\nAlso extended it to accept a bytevector body directly (not just a\nstring), added curry_bytevector_data to the public embedding API so\nthat copy can be a real memcpy instead of a byte-at-a-time loop, and\ngave the internal malloc a NULL check.\n\nNew tests/s3_tests.scm (34 checks: client/URI-encoding/presign-format/\nXML-fixture coverage, plus an opt-in live round-trip gated on env vars,\nskipped by default) registered as ctest s3. New docs/reference/\nmodule-s3.md; module-storage.md and modules.md updated accordingly.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-16T09:31:08+10:00",
          "tree_id": "576f06fd0a24c20be3148f13232bea975d74868b",
          "url": "https://github.com/deconstructo/curry/commit/86d59164d1d0089f223c6b2bf420f21c612e53e0"
        },
        "date": 1786836715443,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 15.183,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.451,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.188,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 25.922,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 130.721,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 260.429,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 58.821,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 83.742,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.053,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "98e046e8d7e082bc07c99e33dfe82ca549d7780f",
          "message": "Merge pull request #55 from deconstructo/track0-module-isolation-tests\n\ntest(modules): add negative export/import isolation regression tests",
          "timestamp": "2026-08-16T18:36:50+10:00",
          "tree_id": "dafe5752984726eb122c25764a564d0896b1269a",
          "url": "https://github.com/deconstructo/curry/commit/98e046e8d7e082bc07c99e33dfe82ca549d7780f"
        },
        "date": 1786869464551,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.103,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.046,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.042,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.44,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 190.648,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 372.534,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.414,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 120.587,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 99.418,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "7f15b947a87a261bc60c95d06226f1acaa2991e5",
          "message": "Merge pull request #56 from deconstructo/track1-target-env-plumbing\n\nfeat(vm): thread a per-chunk target environment through Chunk/Compiler/VM",
          "timestamp": "2026-08-16T18:38:13+10:00",
          "tree_id": "f757d41a57a584e7d812312d9d662eba1695cac3",
          "url": "https://github.com/deconstructo/curry/commit/7f15b947a87a261bc60c95d06226f1acaa2991e5"
        },
        "date": 1786869546474,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 23.309,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 30.155,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.699,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.59,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 192.763,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 380.254,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 72.893,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.331,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 101.872,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "54ddd84bfcb0d6c75ea6619bf8e0d2837e68a42e",
          "message": "Merge pull request #57 from deconstructo/track2-vm-eval-library-bodies\n\nfeat(vm): switch define-library bodies to compile+vm_run (Track 2)",
          "timestamp": "2026-08-16T21:05:53+10:00",
          "tree_id": "49767b32063bcadd43b2641a3eef511d1d46c44a",
          "url": "https://github.com/deconstructo/curry/commit/54ddd84bfcb0d6c75ea6619bf8e0d2837e68a42e"
        },
        "date": 1786878402208,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.167,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 23.461,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.635,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 27.004,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 181.972,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 328.858,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.135,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 106.396,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 95.601,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "186f4ebbc79069fd23c09a19e9e55d12924eb7b9",
          "message": "Merge pull request #58 from deconstructo/jit-fallback-vm-eval\n\nfeat(llvm): swap JIT-failure fallback from eval() to vm_eval()",
          "timestamp": "2026-08-17T03:04:08+10:00",
          "tree_id": "35af52d9d6f3169a77a110ef76eca4af2316c3bd",
          "url": "https://github.com/deconstructo/curry/commit/186f4ebbc79069fd23c09a19e9e55d12924eb7b9"
        },
        "date": 1786899903466,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 25.123,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 24.967,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 7.194,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.591,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 215.974,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 387.894,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.479,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 119.235,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 107.357,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "07a00c635b183035ebd08bca6a4b593aa030118f",
          "message": "Merge pull request #59 from deconstructo/deconstructo-patch-1\n\nDelete docs/thoughts/sql-abstraction-design.md",
          "timestamp": "2026-08-18T18:39:21+10:00",
          "tree_id": "6a8fa4db5f42920f7e0bb7ddd94947d667313365",
          "url": "https://github.com/deconstructo/curry/commit/07a00c635b183035ebd08bca6a4b593aa030118f"
        },
        "date": 1787042399137,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 24.339,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 26.248,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 7.12,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.472,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 203.897,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 366.835,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.833,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.71,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 105.343,
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
          "id": "83d15bb6b221dd416f839734bad1d55ee6824e4f",
          "message": "docs: update maturity-roadmap.md against current main\n\nThe July 16 version was significantly stale: five of its nine gaps (CI,\nerror location/backtrace, LSP, syntax highlighting, interactive\ndebugger, CONTRIBUTING.md) have since shipped. Re-verified every item\nagainst current main and re-ranked what is actually left: widening\nstructured error-code coverage past its current ~17%, publishing a\nquantified R7RS conformance number, LSP go-to-definition plus\npublishing the VS Code extension, backtrace support for user-signalled\nCondition objects, fuzzing, and the still-deferred package manager.\n\nAdded an explicit version/date header (v2, 2026-08-18) tied to the\ncurry release the audit was run against.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-18T18:40:46+10:00",
          "tree_id": "c78dac4f074a1fe7e3344d368fa983502fdbd230",
          "url": "https://github.com/deconstructo/curry/commit/83d15bb6b221dd416f839734bad1d55ee6824e4f"
        },
        "date": 1787042498025,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 23.943,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 30.182,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.388,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 196.031,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 375.697,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.926,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.459,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 102.49,
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
          "id": "0f4007da5de0a938f2ab0008fe5576fd1d57fa50",
          "message": "docs: update performance-chez-kaappi.md against current main\n\nRe-verified the July 16 state table against source. Tier 0 (both\nitems: benchmark CI, --timings) and Tier 1.4 (transparent .scc\ncaching) have shipped since July. The eval-elimination migration made\nreal, if not originally itemized, progress on Tier 4's stated\nprerequisite (\"shrink tree-eval passthrough first\"): define-library\nand R6RS-library bodies now compile and run through the VM instead of\nbeing tree-walked, and the LLVM JIT-compile-failure fallback now\nroutes to the VM too. Flagged the benchmark CI regression gate as a\nsingle-sample comparison confirmed noise-prone on at least one PR this\nsession, rather than presenting it as fully trustworthy. Tier 2 (IR\nlayer) and Tier 3 (GC) remain unchanged.\n\nAdded the same version/date header convention as the companion\nmaturity-roadmap.md update (v2, 2026-08-18).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-18T18:57:21+10:00",
          "tree_id": "f6fe2ce1e76ca4b3826cefca83e4a15e203d0f72",
          "url": "https://github.com/deconstructo/curry/commit/0f4007da5de0a938f2ab0008fe5576fd1d57fa50"
        },
        "date": 1787043484619,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 23.442,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.48,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 6.458,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.187,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 196.769,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 368.891,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 71.144,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 121.008,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 100.278,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "f069de0d71cca325fad02a5895adc8ac7e7c88f7",
          "message": "Merge pull request #60 from deconstructo/tier1-vm-quick-wins\n\nperf(vm): Tier 1 quick wins — self-tail-call, fused global calls, cached tree-eval\n\nThis has been through so many rounds of review. Improvement in performance",
          "timestamp": "2026-08-19T20:16:13+10:00",
          "tree_id": "6e6606128d7efadae7257e8325f8a2ebde7f7eb1",
          "url": "https://github.com/deconstructo/curry/commit/f069de0d71cca325fad02a5895adc8ac7e7c88f7"
        },
        "date": 1787134626360,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.122,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.133,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.738,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.621,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 150.564,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 305.29,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.11,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 102.775,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 83.504,
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
          "id": "4e37e7c11208bcb0b18fa822cdb26b824043abb2",
          "message": "docs: update performance-chez-kaappi.md - Tier 1 done, generational-GC bug noted\n\nMarks all three Tier 1 items (self-tail-call, fused global calls, cached\ntree-eval) done per PR #60, with measured results. Also records the\npre-existing generational-GC stale-pointer bug found while landing that PR\n(tiny-nursery + import + .scc write), so it is not lost before --gc\ngenerational gets its own audit pass.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-19T20:19:41+10:00",
          "tree_id": "812e799c199b682705ff996fae679b12b5c55b71",
          "url": "https://github.com/deconstructo/curry/commit/4e37e7c11208bcb0b18fa822cdb26b824043abb2"
        },
        "date": 1787134824347,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.931,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 30.354,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.285,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.291,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 149.447,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 316.421,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.567,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 101.411,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 78.624,
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
          "id": "17baa6d68408d4c3b6763695bd4fa970894597cb",
          "message": "docs: re-audit eval-elimination migration plan against current main\n\nEvidence-based status pass (file:line citations, not memory) on all 8\nphases of the tree-walker-elimination plan. 6 of 8 done or mostly done.\nTwo real blockers remain: scm_load() and load_scheme_module() still call\neval() per form (phases 4-5), and a new gap found during the audit that\nwas not on the original phase-3 list - define-values/defined? have zero\ncompiler codegen at all, not even a tree-eval punt, which blocks phase 7\noutright. Also flags two callers the plan never addressed (the MPFR\nwith-precision bootstrap eval() call, now actually unblocked since\ndefine-syntax went VM-native; and prim_eval, the permanent R7RS eval\nprimitive) and a stale CLAUDE.md test-doc line.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-19T20:33:32+10:00",
          "tree_id": "dc9e50fcbd2b51cd3d652ecfa0cba030ce292ebf",
          "url": "https://github.com/deconstructo/curry/commit/17baa6d68408d4c3b6763695bd4fa970894597cb"
        },
        "date": 1787135672787,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.612,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 30.11,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.354,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.685,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 148.875,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 314.351,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.079,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 100.601,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 79.331,
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
          "id": "ee8030b19c4f0da0905436c0abedf29c42d89c44",
          "message": "docs: rename eval-elimination migration plan to reflect 2026-08-19 audit\n\nFile content was substantively re-audited and updated against current\nmain in the previous commit; renaming to match so the filename does not\nclaim a stale July date for what is now an August status re-check.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-19T20:35:31+10:00",
          "tree_id": "5e865d1b17ad159d7b59d6e0b81e0940832e2039",
          "url": "https://github.com/deconstructo/curry/commit/ee8030b19c4f0da0905436c0abedf29c42d89c44"
        },
        "date": 1787135779119,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.636,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 30.128,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.335,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.124,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 148.408,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 313.081,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.416,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 101.582,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 78.196,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "0c45c26719bfdc6babf3827f7b80288cd29b9ef9",
          "message": "Merge pull request #61 from deconstructo/tier2-ir-layer\n\nfeat(compiler): Tier 2.1 IR skeleton, verified via differential self-check",
          "timestamp": "2026-08-20T18:19:19+10:00",
          "tree_id": "cb987b1c1180af693350b26ab3988d8ddd0e123c",
          "url": "https://github.com/deconstructo/curry/commit/0c45c26719bfdc6babf3827f7b80288cd29b9ef9"
        },
        "date": 1787214003790,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.833,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.996,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.389,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.195,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 151.766,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 318.873,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.781,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 101.817,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 84.128,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "a152c951e2683a7c4c8d5c3e8aca56ebdabeda8c",
          "message": "Merge pull request #62 from deconstructo/define-values-defined-p\n\nfeat(compiler): native codegen for define-values and defined?",
          "timestamp": "2026-08-20T18:50:19+10:00",
          "tree_id": "4d875ec5accb757bc48259a1d7706e8798c13804",
          "url": "https://github.com/deconstructo/curry/commit/a152c951e2683a7c4c8d5c3e8aca56ebdabeda8c"
        },
        "date": 1787215870235,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 13.826,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.539,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.681,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 25.622,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 105.343,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 239.68,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 54.754,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 70.511,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 57.082,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "be153aeb4abc2335876ec93cc789cf1551a50186",
          "message": "Merge pull request #63 from deconstructo/tier2-ir-widen-1\n\nfeat(compiler): widen Tier 2.1 IR + land first Tier 2.2 optimization\n\napparent performance drop - but this will be accepted",
          "timestamp": "2026-08-21T19:34:43+10:00",
          "tree_id": "d2de00c7e0da0c7efe19fa6a4241d84e63a9a2b8",
          "url": "https://github.com/deconstructo/curry/commit/be153aeb4abc2335876ec93cc789cf1551a50186"
        },
        "date": 1787304928944,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.106,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.509,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 23.479,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 125.244,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 243.66,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 52.805,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 80.896,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 64.311,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "04c384937ede8233426ae753bd16d63aea77a054",
          "message": "Merge pull request #64 from deconstructo/tier2-ir-widen-1\n\nfeat(compiler): and/or simplification, let/named-let IR widening, cache fix",
          "timestamp": "2026-08-21T21:13:21+10:00",
          "tree_id": "fa47ff28af267999ee8b30e4a6f0b9ae53da97e2",
          "url": "https://github.com/deconstructo/curry/commit/04c384937ede8233426ae753bd16d63aea77a054"
        },
        "date": 1787310853582,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 13.961,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 21.064,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.621,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 25.563,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 107.09,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 247.377,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 56.528,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 72.93,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 57.388,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "a06f5288769eafe436aa503d9c90ab11486ef3ed",
          "message": "Merge pull request #65 from deconstructo/fix-delay-force-issue-51\n\nfix(builtins): force fully flattens delay-force chains of any depth",
          "timestamp": "2026-08-21T22:04:27+10:00",
          "tree_id": "d42f77c8f85d655559f8ce5c2cae795e1bb944f1",
          "url": "https://github.com/deconstructo/curry/commit/a06f5288769eafe436aa503d9c90ab11486ef3ed"
        },
        "date": 1787313923913,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.556,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.648,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.248,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.379,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 151.086,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 308.277,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.355,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 101.237,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 77.862,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "c144ad987f2b503f1ec5bdf81b801b047bf402f2",
          "message": "Merge pull request #66 from deconstructo/tier2-ir-widen-1\n\nfeat(compiler): wire the Tier 2.1/2.2 IR into compile()'s live dispatch",
          "timestamp": "2026-08-21T22:09:06+10:00",
          "tree_id": "ca1a6e7418a19456631d19b62b25a71b6261ecb2",
          "url": "https://github.com/deconstructo/curry/commit/c144ad987f2b503f1ec5bdf81b801b047bf402f2"
        },
        "date": 1787314203168,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 12.046,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.428,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.143,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 28.062,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 90.17,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 203.972,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 46.331,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 61.095,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 48.547,
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
          "id": "6ed678c91ecf0cc8435d3fdf6f177f721e5bba9c",
          "message": "chore(formula): update sha256 for v1.22.0 tarball",
          "timestamp": "2026-08-22T01:50:30+10:00",
          "tree_id": "3c9ea3509a92f2a066097a3275a7dee374eba242",
          "url": "https://github.com/deconstructo/curry/commit/6ed678c91ecf0cc8435d3fdf6f177f721e5bba9c"
        },
        "date": 1787327481942,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.645,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.251,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.366,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.192,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 149.646,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 309.161,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.17,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 101.486,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 79.449,
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
          "id": "ea2815dfdba150ff8c38d5d2e2a96d0de614e830",
          "message": "fix(build): resolve Qt6 on Homebrew via qtbase, not the broken qt@6 alias\n\n`brew --prefix qt@6` resolves to Homebrew's umbrella \"qt\" formula, whose\nlib/cmake/Qt6/ doesn't actually contain Qt6Config.cmake (only its qtbase\ndependency ships it), so find_package(Qt6 ...) silently missed and the\nmodule was skipped with just a WARNING. CMakeLists.txt now falls back to\n`brew --prefix qtbase` on macOS when the initial lookup misses, prepending\nit so it isn't shadowed by a qt@6 prefix already on CMAKE_PREFIX_PATH.\n\nFormula/curry.rb had the same bug in Ruby form (--with-qt6 failed to\nconfigure at all): it never added qtbase's opt_prefix, and once added,\nqtbase has to precede qt@6 in CMAKE_PREFIX_PATH or CMake resolves the\nQt6CoreTools sub-config from the broken umbrella tree instead.\n\nOnce found, qt6 also bakes in Homebrew's Qt plugin directory (macOS only)\nso (import (curry qt6)) doesn't abort with \"Could not find the Qt platform\nplugin cocoa\" — no QT_QPA_PLATFORM_PLUGIN_PATH env var needed anymore.\n\nVerified with a from-scratch reconfigure/build of qt6+llvm+ffi+ldap, the\nfull ctest suite (106/106), and a real `brew install --build-from-source`\nagainst the fixed formula.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-22T07:41:36+10:00",
          "tree_id": "b2c9f70807a19a97083366d618ae448290437a25",
          "url": "https://github.com/deconstructo/curry/commit/ea2815dfdba150ff8c38d5d2e2a96d0de614e830"
        },
        "date": 1787348560535,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.82,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.163,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.277,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.385,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 152.035,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 313.123,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 69.072,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 101.875,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 80.171,
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
          "id": "9362b926f65aee52c5d161383dec263a215ad952",
          "message": "chore(formula): update sha256 for v1.22.1 tarball",
          "timestamp": "2026-08-22T07:56:01+10:00",
          "tree_id": "2a68675636305c568ecbf98de2f068e6878447a7",
          "url": "https://github.com/deconstructo/curry/commit/9362b926f65aee52c5d161383dec263a215ad952"
        },
        "date": 1787349404122,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.313,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.147,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.264,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 35.674,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 149.995,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 308.532,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.652,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 101.192,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 78.128,
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
          "id": "0db2e68af189b49907141e6d6d49d9bc10feceb9",
          "message": "fix(bench): stop actor-ring benchmarks from deadlocking on completion signal\n\nAll three actor-ring benchmarks (tests/bench.scm, bench_heavy.scm,\nbench_torture.scm) used a plain set!'d \"finished\" variable, captured by\nthe spawned actor closures, to signal ring completion back to the main\nthread waiting on a condvar.\n\nspawn() deliberately deep-copies every upvalue into the new actor's own\nclosure (vm_snapshot_closure_for_escape in src/vm.c) so that a same-thread\nsibling closure sharing a live loop variable never observes a spawned\nactor's later mutations -- this fixed a real race previously. But it means\na spawned actor's (set! finished #t) only ever mutates its own private\ncopy of that binding, never the main thread's. The main thread's wait loop\nre-checks its own (always-false) finished after the one-and-only signal\nand blocks on the condvar forever.\n\nConfirmed while benchmarking v1.22.1: tests/bench.scm hung indefinitely\non actor-ring/8x100 on both Debug and Release builds, reproduced twice.\nA stack sample showed all ring actors idle in actor_receive and the main\nthread parked in cond_wait with near-zero total CPU time, consistent with\nthe ring completing and only the signal never becoming visible -- not an\nactors.c/vm.c bug, which is working exactly as documented.\n\ncurry has no box primitive, so the fix backs \"finished\" with a\nsingle-element vector instead of a plain variable: vectors are heap\nobjects referenced by pointer, so spawn's upvalue copy shares the same\nunderlying vector across actor and main thread, and vector-set!/vector-ref\nthrough it stay genuinely synchronized (already guarded by the existing\ndone-mtx).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-22T10:39:31+10:00",
          "tree_id": "75ca6644d88319e4dc81dba4bbd23e4223574bdb",
          "url": "https://github.com/deconstructo/curry/commit/0db2e68af189b49907141e6d6d49d9bc10feceb9"
        },
        "date": 1787359221736,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.152,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.675,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.592,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.26,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 149.811,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 310.112,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 68.421,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 101.56,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 77.546,
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
          "id": "41620341b7b0f31fce076986757fd15293900f37",
          "message": "docs(llm): fix broken actor-based summarise-all example in guide-llm.md\n\nThe \"streaming pipelines with actors\" example spawned one actor per text,\nthen collected results with (map (lambda (a) (receive)) actors). That's\nbroken: (receive) reads the CALLING actor's own mailbox, and the\ntop-level/script thread isn't an actor, so actor_self() returns #f and\nactor_receive short-circuits to #f immediately instead of blocking (see\nprim_receive/actor_receive in src/builtins.c and src/actors.c). Verified\ndirectly: swapping llm-ask for a dummy string function and running the old\nexample prints three #f's instantly — every summary was silently\ndiscarded, not just slow.\n\nWorkers can't send! the result back to the caller either, for the same\nreason: the caller has no mailbox to send to.\n\nRewrote the example using the same shared-state idiom the actor-ring\nbenchmarks use (just fixed in 0db2e68): a shared results vector and a\nmutex/condvar pair from (curry sync), with a \"remaining\" counter (also a\nvector, since spawn deep-copies captured variables into the actor's own\nclosure — only a heap object captured by reference stays genuinely shared\nacross that boundary). Verified the corrected pattern against the same\ndummy substitution: all 5 summaries come back in order, no hang.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-22T14:45:48+10:00",
          "tree_id": "937cd649be4bad7938e04724934b6c6005c2870d",
          "url": "https://github.com/deconstructo/curry/commit/41620341b7b0f31fce076986757fd15293900f37"
        },
        "date": 1787373987492,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.377,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 26.615,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.714,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 31.35,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 159.689,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 306.022,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.752,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 103.419,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 81.275,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "c4786a6840ef238df10da0168486365aaaac482c",
          "message": "Merge pull request #67 from deconstructo/tier2-cp0-inline-1\n\nTier 2.3: cp0-style local inliner (first landing)",
          "timestamp": "2026-08-23T01:24:53+10:00",
          "tree_id": "adee95edce8e41b77ad1a4929f0d7f69c2aecc6e",
          "url": "https://github.com/deconstructo/curry/commit/c4786a6840ef238df10da0168486365aaaac482c"
        },
        "date": 1787412330932,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 21.393,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.634,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.899,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.733,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 157.043,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 316.324,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.273,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 103.445,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 82.845,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "985604c5b515e13e73f9db810fe2e23d5ea65e89",
          "message": "Merge pull request #68 from deconstructo/tier2-let-inline-1\n\nTier 2.4: extend local inliner to let/let*-bound candidates",
          "timestamp": "2026-08-23T19:30:58+10:00",
          "tree_id": "e016292891c1560991eb11860a1e03af52106b49",
          "url": "https://github.com/deconstructo/curry/commit/985604c5b515e13e73f9db810fe2e23d5ea65e89"
        },
        "date": 1787477504458,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.594,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.337,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.273,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.945,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 150.168,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 312.383,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 68.622,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 100.754,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 81.663,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "845e0cc975aee20a5cf33a9818a412cfb73892f1",
          "message": "Merge pull request #69 from deconstructo/tier2-let-wrapper-elide-1\n\nTier 2.4: closure elision for compiler-synthesized wrappers\n\nTonnes of extra tests - including an ubuntu and a fedora build!",
          "timestamp": "2026-08-24T19:39:33+10:00",
          "tree_id": "2a997c71f0e5c5a3ca932721c292bb4982e02600",
          "url": "https://github.com/deconstructo/curry/commit/845e0cc975aee20a5cf33a9818a412cfb73892f1"
        },
        "date": 1787564412930,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 20.164,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.571,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.36,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.646,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 147.415,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 308.987,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.884,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 100.654,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 77.257,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "f4900a23a2ed13c22f809cdfbc7c8b777ee9f6fd",
          "message": "Merge pull request #70 from deconstructo/tier2-unchecked-primitives-1\n\nTier 2.5: open-code car/cdr/cons/pair?/null?/arithmetic-comparison ops\n\nSurvived many reviews!",
          "timestamp": "2026-08-25T18:27:47+10:00",
          "tree_id": "7963bfda410a01a118e7fb192a35c36c3240f06d",
          "url": "https://github.com/deconstructo/curry/commit/f4900a23a2ed13c22f809cdfbc7c8b777ee9f6fd"
        },
        "date": 1787646508430,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.421,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 22.832,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.402,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 26.928,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 120.781,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 273.656,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 62.006,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 76.868,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.258,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "dadd563a7fb3ccd2193601b5bda93ef33fb6129d",
          "message": "Merge pull request #71 from deconstructo/fix-callcc-shadow-stack\n\nfix(eval,builtins): call/cc leaks GC shadow stack, gc_inhibit_count, JIT depth",
          "timestamp": "2026-08-25T18:53:48+10:00",
          "tree_id": "235525e66be4620941783f98e000ae048669c786",
          "url": "https://github.com/deconstructo/curry/commit/dadd563a7fb3ccd2193601b5bda93ef33fb6129d"
        },
        "date": 1787648064466,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.719,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 23.263,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.901,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.3,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 107.539,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 261.897,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 61.333,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 72.011,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 53.178,
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
          "id": "313ceaad7cfc3281b7b3f0e10350d1485ad14f00",
          "message": "docs(performance): plan Tier 2.6 LLVM IR retargeting, reframe the blocker\n\nRestarted planning for the item's own remaining scope (retarget\nsrc/llvm/codegen.cpp at the IR instead of raw S-expressions). The\noriginal framing assumed the blocker was that curry's IR is lazily\nlowered by design and would need to become eager before any consumer\nbesides ir_emit could walk it. Re-reading ir.h's own header comments\nplus checking codegen.cpp's actual structure finds that framing was\nwrong: codegen.cpp already has fully independent scope-tracking\n(CompileCtx::scopes) that was never going to reuse the VM Compiler's\nresolve_local/resolve_upvalue anyway, so IR_VAR_REF's raw-symbol,\nresolved-at-consume-time design is already the right shape for an\nLLVM consumer -- no pre-resolution needed. Likewise IR_LAMBDA/IR_SEQ's\nraw bodies don't need pre-lowering; an LLVM consumer can copy ir_emit's\nown interleaved lower-then-consume pattern verbatim, substituting LLVM\nemission for bytecode emission.\n\nNew plan doc (docs/thoughts/tier2-6-llvm-ir-retargeting-plan-2026-08-25.md)\nlays out a much smaller, lower-risk prerequisite than the original\nframing implied: give codegen.cpp an ir_emit-shaped dispatcher for the\nIR kinds ir.h already lowers natively (Phase A), with IR_FALLBACK\nrouting unchanged to codegen.cpp's own existing raw-S-expression\nhandling for forms with no native IR lowering yet (cond/case/do/etc.)\n-- extending IR coverage to those is real but non-blocking follow-up\nwork (Phase B). Includes a per-node-kind statepoint-safety checklist\n(the actual highest-risk part of this whole project) and three open\nquestions to resolve during implementation. No code changed yet --\nplanning only, tracked in the new doc across sessions.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-25T19:02:44+10:00",
          "tree_id": "7f70de29a3a3c3234a40b4b1e140a1f06b0f37a9",
          "url": "https://github.com/deconstructo/curry/commit/313ceaad7cfc3281b7b3f0e10350d1485ad14f00"
        },
        "date": 1787648616439,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.164,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 19.22,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.925,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 22.937,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 117.152,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 226.802,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 50.653,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 71.864,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 57.36,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "241cf19660c33994b83eeb7bbed5028d8b17bc60",
          "message": "feat(compiler): Tier 2.6 Phase A -- interleaved-lowering session API (#72)\n\n* feat(compiler): Tier 2.6 Phase A -- interleaved-lowering session API\n\nGroundwork for retargeting src/llvm/codegen.cpp at the IR instead of\nraw S-expressions (docs/thoughts/tier2-6-llvm-ir-retargeting-plan-\n2026-08-25.md). compiler_ir_lower_for_jit (already landed) only lowers\none top-level expression per call, against a throwaway Compiler that\nnever survives past that call -- not enough for a consumer that needs\nto lower a whole function body one form at a time, interleaved with\nits own per-form consumption (the same \"lower this form, consume it\nimmediately, then lower the next one\" contract ir_emit's own IR_SEQ/\nIR_LAMBDA cases already rely on, required for internal define-syntax\nregistration ordering).\n\nAdds compiler_ir_session_new_root/new_child/lower_next. Genuinely new\nterritory: every other Compiler in this file lives on one C stack\nframe for one synchronous call; a session Compiler must survive across\nmultiple, separate calls from external (eventually C++) code, so it's\nheap-allocated via gc_alloc_pinned -- the same allocator every other\nlong-lived, GC-participating struct in this codebase uses, needed\nsince Compiler holds live val_t references that must stay traceable\nby Boehm for the session's whole lifetime, unlike a stack Compiler\nonly covered by the conservative stack scan for one call's duration.\n\nVerified during this landing that lowering itself never reads a\nCompiler's locals[]/upvals[]/known[]/local_count/chunk fields -- only\n->enclosing (macro visibility) and ->syntax_locals[] (macro\nregistration) are touched by ir_lower's dispatch, since variable/\ncall-site resolution is entirely an ir_emit-time concern deferred by\ndesign (see ir.h's own comments on IR_VAR_REF/IR_CALL). This session\nAPI deliberately does not populate real locals/upvals for that\nreason -- an LLVM consumer's own separate resolution is what actually\nresolves those nodes once it consumes them, exactly mirroring how\nir_emit's own resolve_local/resolve_upvalue do for the VM backend.\n\nEach lower_next call brackets its own gc_inhibit_minor/gc_resume_minor\npair independently rather than leaving that open across the session --\ndeliberately, after PR #71's call/cc fix found exactly this class of\nbug (a paired inhibit/resume left unbalanced across multiple external\ncalls permanently blocks minor GC on the thread). A raise inside\nlower_next returns NULL without freeing the arena (earlier forms\nalready lowered through the session may still be referenced by the\ncaller) -- the caller owns exactly when to free it, same single-owner\ncontract every other IRArena in this codebase has.\n\nIndependent code review found one real gap: new_root/new_child's\n`name` parameter was stored unowned with no documented lifetime\nrequirement, unlike compiler_set_source_name's own existing \"must\noutlive\" contract for the same class of raw string pointer. A named-\nlet form lowered through a session embeds that same pointer into a\npersisted IR_LAMBDA node kept alive in the session's arena, so a\ntransient name from a future (not-yet-written) caller would leave a\ndangling pointer. Fixed by documenting the same \"literal or\nprocess-lifetime buffer\" requirement explicitly in compiler.h.\n\n347/347 C unit tests, 107/107 ctest suites, fresh --clear-cache run.\nIndependent code-review and security-review passes on the final diff.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* docs(performance): update Tier 2.6 plan session log for PR #72\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-25T19:42:01+10:00",
          "tree_id": "b7ea2f77e21bd4e130d11a354ed4789f2f6f65e6",
          "url": "https://github.com/deconstructo/curry/commit/241cf19660c33994b83eeb7bbed5028d8b17bc60"
        },
        "date": 1787650959195,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.195,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 23.062,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.336,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 27.333,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 121.971,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 271.59,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 62.566,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 76.835,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 64.858,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "e397a532161770e71fa0ddcdc5dd34c42a3f4c42",
          "message": "refactor(compiler): split compiler.c into five files along IR pipeline boundaries (#73)\n\n* refactor(compiler): split compiler.c into five files along IR pipeline boundaries\n\nPure code motion, no behavior change (verified: 332/332 core tests, 100/100\nctest suites, identical to pre-split baseline). compiler.c (5756 lines) mixed\nfour genuinely distinct concerns: the classic pre-IR special-form dispatcher,\nIR lowering, IR bytecode emission, and public API + differential self-check\ntest infrastructure.\n\n- src/compiler.h: added the opaque `typedef struct Compiler Compiler;`\n  forward declaration this split's internal-header pattern depends on\n  (mirrors src/runtime_internal.h's eval.c/runtime.c split.\n- src/compiler_internal.h (new): the real Compiler struct body, SpecialForm\n  enum, shared macros, and declarations for every symbol that crosses the\n  new file boundaries.\n- src/compiler.c (shrunk to ~810 lines): Compiler lifecycle, emit/scope/\n  local/upvalue helpers, and the public API entry points.\n- src/compiler_classic.c (new): the classic compile_* dispatcher, compile(),\n  compile_classic(), compile_seq(), classify_head().\n- src/ir_lower.c (new): ir_lower/ir_lower_*, ir_optimize, and the Tier 2.3\n  local-inliner eligibility/closedness helpers (moved here, not to\n  compiler_classic.c, since call-graph tracing showed they're consulted\n  exclusively by ir_lower_let/ir_lower_let_star, never by classic\n  compile_let).\n- src/ir_emit.c (new): ir_emit + ir_emit_inline_call.\n- src/compiler_ir_checks.c (new): compiler_ir_self_check/\n  compiler_ir_optimize_check/compiler_ir_inline_fired_check.\n\nCMakeLists.txt: added the four new source files to CORE_SOURCES.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nEOF\n)\n\n* fix(compiler): correct opaque Compiler typedef comment\n\nCode review of the compiler.c split flagged that the comment referenced compiler_ir_session_lower_next, a function declared in compiler.h but not yet implemented anywhere in this tree. Reworded to describe the forward declaration without pointing at a nonexistent API.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-25T21:41:38+10:00",
          "tree_id": "6ba6140258c3d40555f7d7c346a7ff2d117cbd82",
          "url": "https://github.com/deconstructo/curry/commit/e397a532161770e71fa0ddcdc5dd34c42a3f4c42"
        },
        "date": 1787658166523,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.201,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 22.557,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.704,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 28.2,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 101.245,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 243.915,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 59.851,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 66.822,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 53.514,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "bea13af425714629743e96d9de4ab308d5209083",
          "message": "feat(network): implement SRFI 106 (Basic Socket Interface) (#74)\n\nCurry already has a richer, curry-specific networking API (tcp-connect/\ntcp-listen/tcp-accept, udp-socket/-bind/-send/-recv, non-blocking mode,\nTLS) that's a strict superset of SRFI 106's scope (SRFI 106 is\nblocking-only, TCP/UDP only, no TLS). Adds SRFI 106 as a thin\ncompatibility layer on top of the same underlying socket handling, for\nportable code written against the standard interface.\n\nNative primitives (make-client-socket, make-server-socket, socket?,\nsocket-accept, socket-send, socket-recv, socket-shutdown, socket-close,\nsocket-input-port, socket-output-port, plus the plain-fixnum named\nconstants) live in modules/network/srfi106.c, registered directly under\nthe SRFI's own names -- same convention (curry posix)/SRFI-170 already\nuses. Shares its raw-socket-handle representation with the existing\ntcp-listen/udp-socket via a new network_internal.h (extracted from\nnetwork.c, which previously had these helpers as file-private statics).\n\nThe shutdown-method flag category needed real design attention, not\njust a mechanical port: real POSIX SHUT_RD/SHUT_WR/SHUT_RDWR are 0/1/2\non every platform checked, NOT independent bits, so combining them the\nsame way address-info/message-type combine their own genuinely-\nindependent AI_*/MSG_* bits would silently produce the wrong value\n((shutdown-method read write) via SHUT_RD|SHUT_WR = 0|1 = 1, colliding\nwith plain SHUT_WR instead of SHUT_RDWR). Fixed by using a clean,\nalways-combinable 1/2/3 encoding for *shut-rd*/*shut-wr*/*shut-rdwr*\ninstead, translated to the real platform constant inside\nsocket-shutdown's own C implementation. Has a dedicated regression test\nverifying (shutdown-method read write) actually differs from\n(shutdown-method write) alone.\n\nThe six name->constant macros (address-family, address-info,\nsocket-domain, ip-protocol, message-type, shutdown-method) and the two\nflag combinators (socket-merge-flags, socket-purge-flags) live in the\nScheme shim (srfi/s106/sockets.scm) as syntax-rules macros -- pure\ncompile-time lookups with no runtime behavior, and curry's C module API\nhas no macro-registration story.\n\nIndependent code review found and fixed three real bugs before this\nlanded:\n- socket-input-port/socket-output-port leaked the dup()'d fd when\n  fdopen failed (curry_make_port_from_fd only takes ownership on\n  success, per its own documented contract) -- same pre-existing gap\n  found in network.c's own tcp-connect/tcp-accept while fixing this,\n  patched there too for consistency.\n- service_to_cstr's 16-byte buffer wasn't sized for curry's actual\n  fixnum range (~61-bit signed, not 32-bit) -- extreme port values were\n  silently truncated by snprintf rather than overflowing (never a\n  memory-safety bug, but a real correctness one: getaddrinfo would\n  reject the truncated string with no hint why).\n- srfi106.c was missing the ws2tcpip.h include network.c already has\n  for its own Windows build (struct addrinfo/getaddrinfo/AI_* are\n  declared there, not in winsock2.h) -- would have failed to compile on\n  Windows.\n\n347/347 C unit tests, 108/108 ctest suites (new srfi_106 suite: 33\nassertions covering real TCP/UDP round trips via both raw socket-send/\nrecv and port-based I/O, all six name macros, flag combinators,\ncall-with-socket's close-on-error guarantee via dynamic-wind, and the\nshutdown-method correctness case above), fresh --clear-cache run.\nIndependent code-review and security-review passes on the final diff.\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-25T23:35:10+10:00",
          "tree_id": "985c2acd875e405a2c9dd5bdc426e9bc473e0e41",
          "url": "https://github.com/deconstructo/curry/commit/bea13af425714629743e96d9de4ab308d5209083"
        },
        "date": 1787664961276,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.92,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 24.733,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.116,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.513,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 138.456,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 287.989,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.635,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.771,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 72.736,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "6a7960b018322c40cd0849ed8d2b9931c6ab5c4c",
          "message": "test(srfi): add real behavioral coverage for (srfi s215 log) (#75)\n\nSRFI 215 (Central Log Exchange) has been implemented since before this\nsession, but was only ever smoke-tested (\"send-log is bound\") via\nsrfi_numbered_shims_tests.scm -- no test verified the actual message\nshape, severity constant values, current-log-fields merging, the\nvalue-conversion rules (string?/bytevector?/exact-integer?/error-object?/\ncondition? kept as-is, everything else written), the error paths (odd\ntrailing args, non-symbol key), or the one genuinely subtle piece of\nthis library: send-log calls made before any application callback is\ninstalled are buffered (up to 100 messages) and replayed in order into\nthe first non-default callback installed afterward.\n\nIndependent code review of an earlier version of this suite found one\nreal test-quality bug: the \"multiple extra pairs, in order\" check used\ntwo separate assq lookups, which find a key regardless of its position\nin the alist -- a swapped-order bug in send-log's own pair-building\nloop would have passed both lookups undetected. Fixed by comparing the\nexact remaining list structure instead; verified this actually catches\nan ordering regression by temporarily removing send-log's own\n(reverse acc) call, confirming the test fails, then restoring it.\n\n31 assertions, all passing. 347/347 C unit tests, 108/108 ctest suites\n(including this new srfi_215 suite), fresh --clear-cache run.\nIndependent code-review pass on the final diff.\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-26T18:53:12+10:00",
          "tree_id": "b998fb1e5a937092b256af0ef04b073ac7bd9991",
          "url": "https://github.com/deconstructo/curry/commit/6a7960b018322c40cd0849ed8d2b9931c6ab5c4c"
        },
        "date": 1787734454742,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.031,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.959,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.807,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.483,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 129.146,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 293.23,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.858,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.36,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.458,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "7d105d98f9f70d048ac96e49021532e63f2feb93",
          "message": "feat(tts): add Piper neural TTS backend with direct-to-speaker playback (#76)\n\n* feat(tts): add Piper neural TTS backend with direct-to-speaker playback\n\nAdds (curry piper), a real C module wrapping libpiper\n(https://github.com/OHF-Voice/piper1-gpl), wired into (curry tts) as\nthe 'piper backend. Unlike the existing 'macos-say/'espeak-ng backends\n(plain CLI tools spawned via (curry posix)), this is genuine native\ncode: libpiper streams synthesized audio via a 9-field struct\n(piper_audio_chunk) with raw float* sample arrays, exactly the \"deep\nstruct traversal\" case docs/reference/module-ffi.md itself says to\nwrite a C module for rather than force through the generic FFI layer.\n\nDirect-to-speaker playback (not just WAV-file output) via native\nplatform audio: CoreAudio's AudioQueue API on macOS, ALSA on Linux\n(optional at build time -- piper-save/WAV output works without it,\npiper-speak-async raises a clear runtime error instead of failing to\nbuild or crashing if ALSA wasn't found). Audio plays as soon as the\nfirst chunk is synthesized, not after the whole utterance completes.\n\nThe background thread piper-speak-async spawns never touches a\ncurry_val or GC-heap pointer at all -- text is strdup'd before the\nthread starts, specifically so it has zero dependency on curry's GC\nbeing aware of it (no gc_register_thread() needed, unlike e.g.\nmodules/mcp/mcp.c's per-connection threads, which do construct\ncurry_vals off-thread and therefore do need it). Verified this\ninvariant by reading the entire thread call graph.\n\n(curry tts)'s own <tts-backend> protocol needed widening: 'piper's\nspeak-async returns a background-thread handle, not a (curry posix)\nprocess handle the way the other two backends' does, so tts-wait/\ntts-stop/tts-speaking? needed to become backend-polymorphic (checking\nprocess-handle? first for the common case, falling back to a\nregistered backend's own wait/stop/speaking?/handle? procs otherwise).\nThe backend table itself also had to become an open registry\n(tts-register-backend!) rather than the fixed set it was, since (curry\ntts piper) may not exist as an importable library at all depending on\nwhether curry was built with -DBUILD_MODULE_PIPER=ON (OFF by default\n-- libpiper has no system package yet, see docs/reference/\nmodule-piper.md for the build steps), unlike macos-say/espeak-ng which\nalways compile in.\n\nVerification note: could not get a working local libpiper build on\nthis machine to link/run against -- its own build dependency,\nespeak-ng, crashes with a trace trap on this specific macOS/clang\nversion, confirmed unrelated to this module's own code by running the\nfreshly-built espeak-ng binary directly. Did verify modules/piper/\npiper.c compiles cleanly (clang -fsyntax-only and a real -c compile to\nan object file, zero warnings with -Wall -Wextra) against the REAL\npiper.h header (cloned from the upstream repo, not retyped from\nmemory) and real CoreAudio headers, and confirmed via nm -u that all\nsix libpiper symbols called are referenced correctly. Could not verify\nactual linking against a compiled libpiper.dylib or any runtime\nbehavior. A manual review pass (the code-review subagent hit the\naccount's monthly spend limit mid-session and couldn't run) found and\nfixed two real bugs before this landed:\n- PiperPlayback (the piper-speak-async handle's backing struct,\n  including its pthread_mutex_t/pthread_cond_t) was never freed on any\n  path -- fixed by freeing it in piper-wait, once playback is\n  confirmed done; documented that a handle never waited on (pure\n  fire-and-forget) still leaks this same small fixed-size struct,\n  matching piper-synth's own already-explicit \"no GC finalizer, call\n  piper-free! yourself\" contract.\n- tts/piper.scm's synth cache was keyed on voice name alone, not\n  (voice-dir . voice-name) -- changing current-piper-voice-dir between\n  two calls using the same voice name would have silently kept\n  returning the first directory's cached synth.\n\n347/347 C unit tests, 107/107 ctest suites (the new piper suite is\ngated behind BUILD_MODULE_PIPER AND TARGET curry_piper, so it doesn't\nexist in ctest's list at all on this or any ordinary build), fresh\n--clear-cache run -- all against the default BUILD_MODULE_PIPER=OFF\nconfiguration, confirming this change doesn't affect anyone who\ndoesn't opt in. Independent security review (the code-review subagent\nabove; security-review ran successfully) found no new external attack\nsurface.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(piper): fix data loss, exit crash, and use-after-free found via first real runtime test\n\nPR #76's previous commit shipped never having actually linked against a\nbuilt libpiper -- espeak-ng, one of its own build dependencies, crashed\non this machine's toolchain. That's now resolved (root cause: espeak-ng\nhardcodes its install path into a fixed 160-byte buffer via an\nunchecked strcpy, src/libespeak-ng/speech.c:338, and this sandbox's\ndefault scratchpad path is long enough to overflow it -- building from\na short path like ~/piper-build sidesteps it entirely; not a beta-\ntoolchain issue as originally guessed). With a real libpiper linked\nand a real voice model downloaded, this is the first time any of\nmodules/piper/piper.c has actually executed, and it surfaced four real\nbugs no amount of syntax-only compilation could have caught:\n\n- piper_synthesize_next's PIPER_DONE return code carries valid sample\n  data in the SAME call, not a separate empty final call (confirmed\n  against libpiper's own reference caller, src/main/utils/wavfile.cpp,\n  which ignores the return code entirely and drives its loop off\n  chunk.is_last). Both piper-save's and piper-speak-async's synthesis\n  loops treated PIPER_DONE as \"stop now, nothing to process,\" silently\n  discarding that last chunk's audio -- for any text short enough to\n  finish in one chunk, that's every sample: piper-save was producing a\n  44-byte WAV (header only, zero audio) on every call.\n\n- Any live synth still allocated when the process exits crashes it:\n  onnxruntime's global OrtEnv singleton throws from its own static\n  destructor during exit()'s teardown sweep if a piper_synthesizer is\n  still alive (confirmed with lldb -- the abort is entirely inside\n  libonnxruntime's unique_ptr<OrtEnv>::~unique_ptr, not this module's\n  code). (curry tts piper)'s synth cache never freed anything, so\n  every process that ever used the piper backend would have aborted on\n  exit. Fixed with a tracked-synth + atexit() sweep -- registered\n  lazily, on first use, specifically so it lands AFTER onnxruntime's\n  own lazy atexit registration (which happens inside the first\n  piper_create() call): atexit runs LIFO, so registering any earlier\n  put our cleanup before onnxruntime's in the list, meaning its crash\n  fired before we ever got a chance to free anything.\n\n- piper-wait was freeing the handle's backing struct (mutex/condvar\n  included) once done. That's wrong: (curry tts)'s tts-stop/tts-wait/\n  tts-speaking? dispatch calls all three on the same handle in\n  sequence for every backend (mirrored from real process handles,\n  which stay valid indefinitely after process-wait) -- freeing inside\n  piper-wait made that a live use-after-free, caught by\n  piper_tests.scm's own round-trip test silently returning the wrong\n  answer instead of crashing outright. Fixed by not freeing there at\n  all; playback handles now behave like process handles (valid\n  indefinitely, reclaimed at exit via the same tracking mechanism as\n  synths, no explicit free primitive).\n\n- piper_create doesn't catch its own internal C++ exceptions before\n  returning across its extern \"C\" boundary -- a nonexistent model path\n  makes it try to parse a missing/empty JSON config and throw\n  std::terminate straight through, crashing the process instead of\n  returning NULL. Can't fix this from a plain-C wrapper without a\n  disproportionate C++ rewrite; mitigated by validating the model/\n  config paths exist before ever calling into piper_create, covering\n  the single most likely real-world trigger (a typo'd path) with a\n  normal catchable curry_error. Documented as a known, not fully\n  closable, upstream gap.\n\nAlso fixed, found in the same pass: tts-speak (the blocking (curry\ntts) convenience wrapper) called process-wait directly instead of the\nalready-widened tts-wait, so it worked for macos-say/espeak-ng (real\nprocess handles) but raised \"not a process handle\" for any non-process\nbackend -- invisible to tts_tests.scm since it only exercises the two\nprocess-handle backends. And lib/curry/modules/curry/tts.scm's\nmake-tts-backend used cadddr, which isn't a core binding in curry\n(only 2-level cxr combinators are guaranteed outside a (scheme cxr)\nlibrary this codebase doesn't have) -- replaced with (car (cdddr ...)).\n\nVerified end-to-end this time, not just compiled: tts-speak/tts-save\nthrough the 'piper backend, direct piper-create/piper-speak-async/\npiper-save/piper-wait/piper-stop!/piper-alive? primitives, real WAV\noutput confirmed non-silent (afinfo/python wave module: 2+ seconds,\n94% non-zero samples), 3x repeated clean-exit runs with and without\nexplicit piper-free!/piper-wait, and the full piper_tests.scm suite\n(12/12) against a real linked libpiper build. Full existing suite\nre-verified for regressions: 107/107 ctest, tts_tests.scm 27/27,\nagainst the ordinary BUILD_MODULE_PIPER=OFF build.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* docs(piper): add build/usage/voice-sourcing guide for the piper TTS backend\n\nAdds docs/guides/tts-piper.md as the narrative walkthrough companion to\nthe existing module-piper.md/module-tts.md reference docs, and cross-links\nit from both plus the README guide index.\n\nLooking forward to hearing some Ancient Greek and Akkadian :-)",
          "timestamp": "2026-08-26T18:55:48+10:00",
          "tree_id": "d7b07e366403b38de141af398532db545198330b",
          "url": "https://github.com/deconstructo/curry/commit/7d105d98f9f70d048ac96e49021532e63f2feb93"
        },
        "date": 1787734598015,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 13.968,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 19.268,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.957,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 23.475,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 108.184,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 222.848,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 50.321,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 70.829,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 55.379,
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
          "id": "5592c0f122222c8b8f2341e22eff9ee41a3cc2f5",
          "message": "chore(formula): update sha256 for v1.23.0 tarball\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-26T19:01:06+10:00",
          "tree_id": "5314aa77659718ca6b5a882280afa2c09a1a85c3",
          "url": "https://github.com/deconstructo/curry/commit/5592c0f122222c8b8f2341e22eff9ee41a3cc2f5"
        },
        "date": 1787734924560,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.076,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 30.66,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.754,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.317,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 125.901,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 285.996,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.384,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.841,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.835,
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
          "id": "d8edbef9781c84ebe73bdad0c00947e0efa8dd71",
          "message": "feat(formula): add --with-piper option, auto-detected under /usr/local\n\nlibpiper/onnxruntime have no Homebrew formula of their own, so there's\nnothing to depends_on -- this searches /usr/local/{include,lib} for\npiper.h/libpiper.*/libonnxruntime.* before running cmake, odie'ing with\nthe build-from-source steps (docs/guides/tts-piper.md) if not found,\notherwise passing -DBUILD_MODULE_PIPER=ON -DPIPER_ROOT=/usr/local.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-26T19:10:18+10:00",
          "tree_id": "6e8d537cde061b5aa6a4137396b852d06d23b9a5",
          "url": "https://github.com/deconstructo/curry/commit/d8edbef9781c84ebe73bdad0c00947e0efa8dd71"
        },
        "date": 1787735463070,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.448,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 32.374,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.553,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.205,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 129.963,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 287.867,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.223,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.416,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.748,
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
          "id": "7265c905d9c455f8e83b76eb7a4533355a9db67b",
          "message": "fix(formula): correct v1.23.1 sha256 (previous commit dropped last char)\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-26T19:17:19+10:00",
          "tree_id": "a655fb96178ad49f78c35e62a21ecb47377ae4d5",
          "url": "https://github.com/deconstructo/curry/commit/7265c905d9c455f8e83b76eb7a4533355a9db67b"
        },
        "date": 1787735890538,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.483,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.804,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.688,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.567,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 130.002,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 281.374,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.195,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.108,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.374,
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
          "id": "3067acc01b841727e0fdbe063e480b22e804408c",
          "message": "fix(formula): stage piper.h/libpiper/libonnxruntime out of /usr/local for the build\n\nHomebrew's superenv build shim (shims/mac/super/bin/clang) silently\ndrops any -I/-L flag under /usr/local whenever Homebrew's own prefix\nis elsewhere (true here: /opt/homebrew), treating it as stray\nIntel-Homebrew pollution -- even though CMake's own find_path/\nfind_library (which run outside the shim) succeed at configure time\nand the flag shows up verbatim in the printed build log. The result\nwas a misleading \"piper.h file not found\" despite the file genuinely\nexisting and the flag genuinely being on the command line, confirmed\nby manually reproducing the exact compile invocation both with and\nwithout the shim.\n\nFix: copy the three files into a build-local .piper-stage directory\n(anywhere outside /usr/local satisfies the shim's filter and point\n-DPIPER_ROOT there instead. Runtime resolution is unaffected -- both\ndylibs' own LC_ID_DYLIB is @rpath-relative (confirmed via otool -D),\nand macOS's default DYLD_FALLBACK_LIBRARY_PATH already includes\n/usr/local/lib, so the installed curry binary finds the real files at\nruntime regardless of which copy it linked against at build time.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nEOF\n)",
          "timestamp": "2026-08-26T19:24:38+10:00",
          "tree_id": "f9849fee5f409df88f40ebd75256880e48d469c9",
          "url": "https://github.com/deconstructo/curry/commit/3067acc01b841727e0fdbe063e480b22e804408c"
        },
        "date": 1787736320697,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.515,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.543,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.616,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.261,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 130.109,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 282.783,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.527,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 86.08,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.023,
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
          "id": "e20cbb8f451443259c4b2936495e557b8f352f59",
          "message": "feat(formula): package libpiper as a real Homebrew formula\n\nReplaces the /usr/local staging workaround with a proper dependency:\nFormula/libpiper.rb builds libpiper1-gpl's libpiper subdir (which\nneeds network access during install -- it clones espeak-ng and\ndownloads a prebuilt onnxruntime release itself, neither pre-fetched\nas a Homebrew resource) and fixes up both dylibs' install names\nafterward.\n\nBoth libpiper.dylib and onnxruntime's dylib ship with an @rpath-\nrelative LC_ID_DYLIB, unlike every other Homebrew-packaged dep curry\nlinks against (openssl, libgit2, ...), which all use absolute install\nnames and therefore never needed an rpath. Without fixing this,\ndlopen()'ing piper.so fails at runtime with \"no LC_RPATH's found\" --\nconfirmed by hitting exactly that failure twice: once for libpiper's\nown -id (fixed via install_name_tool -id), and again for libpiper.\ndylib's own internal LC_LOAD_DYLIB reference to onnxruntime, which\n-id does nothing for and needed a separate install_name_tool -change.\nVerified end to end: `curry -e '(import (curry piper))\n(display (piper-version))'` prints 1.7.0 with no rpath anywhere.\n\ncurry.rb now just depends_on \"libpiper\" if build.with? \"piper\",\ndropping the whole .piper-stage staging block from the previous\ncommit -- that workaround only existed to dodge Homebrew's superenv\nbuild shim filtering -I/-L flags under /usr/local (see that commit's\nown message; a real formula living under Homebrew's own prefix\nsidesteps the filter entirely, no staging needed.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nEOF\n)",
          "timestamp": "2026-08-26T20:38:01+10:00",
          "tree_id": "7d55f69494ccdcaeb70ca71a28da8d5f2046c97a",
          "url": "https://github.com/deconstructo/curry/commit/e20cbb8f451443259c4b2936495e557b8f352f59"
        },
        "date": 1787740751150,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.65,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.274,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.674,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.306,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 130.558,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 305.888,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 83.285,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 90.42,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 70.236,
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
          "id": "883b3aa9af62cdd3af81ac9a20ba1eada27c6df5",
          "message": "fix(tts): find espeak-ng-data under the new libpiper Homebrew formula\n\n%find-espeak-data's search list predated Formula/libpiper.rb -- on a\nmachine with only that formula installed (no separate espeak-ng), the\nlookup fell through to #f, and piper-create fell back to a stale\nbuild-tmp path baked into libpiper.dylib at its own build time\n(\"Error processing file '.../libpiper-<tmp>/.../espeak-ng-data/\nphontab': No such file or directory\"), found by testing piper support\nmanually end to end (piper-save, tts-save, tts-speak) rather than just\npiper-version. Added the formula's own opt-prefix data dir ahead of\nthe existing candidates.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-26T20:46:19+10:00",
          "tree_id": "cf947544ed84d228790d17d5333e6ff57361d1df",
          "url": "https://github.com/deconstructo/curry/commit/883b3aa9af62cdd3af81ac9a20ba1eada27c6df5"
        },
        "date": 1787741269215,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.165,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 19.369,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.964,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 23.005,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 107.673,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 226.012,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 51.213,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 70.106,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 54.244,
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
          "id": "b965a03f6a57ba6d6b052f9d2f2f78719433d42d",
          "message": "fix(errors,port): print (curry conditions) objects properly at top level\n\nAny uncaught CL-style condition (condition-error, e.g. tts-error) hit\nthe top-level error reporters generic fallback -- it only special-\ncased R7RS ErrorObj (T_ERROR), not Condition (T_CONDITION) -- and the\ngeneric writer had no print case for T_CONDITION either, so the\nfallback rendered a bare #<object 46> (46 = T_CONDITION tag) with\nno indication of what actually failed or why.\n\nFound via manual end-to-end piper testing: setting the TTS backend to\npiper and calling tts-speak with no voice directory configured raises\nexactly this kind of condition, and it printed as that bare object tag\ninstead of the actual no piper voices found message already carried\nin the conditions own message field.\n\nprint_scheme_error (main.c) now checks vis_condition() first and\nprints the conditions type_sym plus message the same way it already\ndoes for ErrorObjs code plus message. ports generic writer also\ngains a condition-name case, so display and write on a condition\nnever fall through to the raw tag-number fallback either, independent\nof where the condition ends up.\n\n102/102 ctest suites pass (fresh clear-cache run); verified the\npiper-specific reproduction directly against the built curry binary\nbefore and after the fix. Independent low-effort code review found no\nissues.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-26T21:01:00+10:00",
          "tree_id": "b992ad7016caa7e30d2400d2c0d532b620455190",
          "url": "https://github.com/deconstructo/curry/commit/b965a03f6a57ba6d6b052f9d2f2f78719433d42d"
        },
        "date": 1787742129400,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.424,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.991,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.6,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.929,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 125.517,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 294.744,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.629,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.58,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.471,
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
          "id": "2ba9d6d6e4116a9a97aa1458f757671e10c49d4d",
          "message": "fix(tts): espeak-ng backend passed the wrong column to -v, breaking every #:voice value\n\nespeak-tts-voices built its (name . locale) pairs as (VoiceName .\nLanguage) -- e.g. (English_(America) . en-us) -- and name is what\n(curry tts)'s own contract requires as a valid #:voice value passed\nstraight through to espeak-ng -v. Real espeak-ng only accepts the\nLanguage column there; passing the VoiceName column fails with The\nspecified espeak-ng voice does not exist. This meant #:voice had never\nactually worked for any value taken from the backends own tts-voices\noutput, for any language, since this file was written -- only ever\nmanually verified with #:voice unset.\n\nFound while checking whether Irish (Gaeilge) TTS is possible through\ncurry: espeak-ng genuinely bundles an Irish voice (cel/ga in its own\ndata), but #:voice ga failed with that exact does not exist error\nuntil this fix. Now both fields of the pair are the Language column,\nsince thats the only value espeak-ng -v actually accepts -- confirmed\nend to end (a real Irish WAV file synthesized and played back), plus\nre-verified English still works and current-tts-language auto-\nselection is unaffected (it already matched against the Language\ncolumn via the cdr, so this fix only changes what car returns).\n\ndocs/reference/module-tts.md updated to match: the tts-voices example\noutput, the current-tts-language example voice name, and the Notes\nsection explaining the naming-convention difference between backends.\n\n102/102 ctest suites pass (fresh clear-cache run). Independent\nlow-effort code review found no issues.\n\nAlso mentioned separately: macOS say does have a genuine Irish\nEnglish voice, Moira (English (Ireland)) / en_IE, confirmed working\nthrough (curry tts) -- no code change needed there, just missed on\nthe first pass since only Pipers voice catalogue was checked.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-27T04:23:14+10:00",
          "tree_id": "569f7855df820463eb36ba80eb176807abda3004",
          "url": "https://github.com/deconstructo/curry/commit/2ba9d6d6e4116a9a97aa1458f757671e10c49d4d"
        },
        "date": 1787768714378,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.785,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.677,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.264,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.736,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 141.179,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 282.63,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.214,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.195,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 71.833,
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
          "id": "72b8b3d391ce8222876ae432ab4bb6169592bd3c",
          "message": "chore(formula): update sha256 for v1.23.2 tarball\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-27T04:26:10+10:00",
          "tree_id": "57b1bb72a555212dd4c9d35326aef78dcbdbef8f",
          "url": "https://github.com/deconstructo/curry/commit/72b8b3d391ce8222876ae432ab4bb6169592bd3c"
        },
        "date": 1787768823893,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.974,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 22.997,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.402,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 27.397,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 122.534,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 275.18,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 63.319,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 77.638,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.274,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "c17898b6d7a52a5779ee3f4c6a9c68f90019bd5c",
          "message": "fix(random): SRFI-27 pseudo-randomize!/random-source->random-integer, plus RNG thread-safety (#77)\n\nFound while adding deterministic-seed test coverage for the upcoming\n(curry gillespie) module: random-source-pseudo-randomize! (3 args, an\ninteger seed pair per SRFI 27) was bound to the exact same primitive\nas random-source-randomize! (1 arg, reseed unpredictably from the OS)\n-- it ignored its seed arguments entirely and reseeded from\n/dev/urandom every time. Every value it claimed to make reproducible\nwas actually still random. Fixed by giving it its own primitive that\nexpands the two integer seeds into xoshiro256+'s four-word state via\nsplitmix64, the same construction xoshiro's own reference\nimplementation recommends for seeding from a small value.\n\nAlso found and fixed: random-source->random-integer was bound to the\nsame primitive as random-source->random-real, so it returned a\nzero-argument real-number generator instead of a one-argument integer\ngenerator.\n\nSeparately: the RNG state (rng_s/rng_seeded) was a bare global with no\nsynchronization at all, despite curry having real OS-thread actors\nthat can call random-real/random-integer concurrently -- a genuine\ndata race, not hypothetical, given the multi-cell Gillespie design\nthis was found while preparing for is explicitly \"each cell is an\nactor drawing its own random waiting times\". Every access now goes\nthrough rng_mutex.\n\nAdded regression tests to tests/random_tests.scm for both binding\nfixes (same seed -> identical sequence, different seed -> different\nsequence, random-source->random-integer actually produces bounded\nintegers). 102/102 ctest suites pass (fresh --clear-cache run).\nIndependent medium-effort code review verified by actually building\nand running the suite rather than static reading alone; no findings.\n\nI was stupid enough to not really check for complete coverage of SRFI-27. Lesson: AI can do cool things, but one should remain in the loop.",
          "timestamp": "2026-08-27T19:15:29+10:00",
          "tree_id": "767a0b4128377006013a6ac6c2a8e81c9a809f11",
          "url": "https://github.com/deconstructo/curry/commit/c17898b6d7a52a5779ee3f4c6a9c68f90019bd5c"
        },
        "date": 1787822175102,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.301,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.288,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.141,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.613,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 139.586,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 292.338,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.3,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.473,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 67.358,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "26ae094d6b607efeb9d9c5d2f896f927f0f3c56c",
          "message": "feat(gillespie): add (curry gillespie) stochastic cell-biochemistry module (#78)\n\n* docs(thoughts): design sketch for a toy evolution model composing with Gillespie\n\nParks the population-genetics idea from conversation: genome as a\nvector of alleles, composable mutation operators (point/indel/\nduplication), three distinct modes of gene transfer (vertical asexual,\nvertical sexual/crossover, horizontal transfer), and standard roulette/\ntournament selection.\n\nThe part worth writing down carefully: a genome can literally be a\nGillespie cells vector of reaction rate constants, making fitness the\nresult of actually running that cells own biochemical simulation --\nmutation perturbs rate constants, sexual reproduction mixes two\nlineages rate vectors, and horizontal transfer splices one cells\nsuccessful rate constant into an unrelated cells network mid-\nsimulation (a reasonable toy model of plasmid-mediated resistance\nspread). The shared Gillespie environment (temperature/pH/nutrients)\nthen gives environmental selection pressure for free, since fitness is\ndefined by running the real simulation under that environment.\n\nPre-implementation only -- parked for later per explicit request, not\nwired into anything yet.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* docs(thoughts): Gillespie cell-model design, including an SBML import angle\n\nWrites up the design already discussed for (curry gillespie): composable\npropensity/rate-law combinators (mass-action, arrhenius, michaelis-\nmenten), the reaction/cell/environment record shapes, why environment\nsensitivity (temperature/pH/nutrients) falls out for free from\npropensities just being functions of state, multi-cell simulation via\ncurry's existing actor+STM concurrency rather than new machinery, and\na Qt visualization sketch.\n\nAdds an SBML interoperability section per request: curry already has\nthe two pieces a real importer would need -- (curry xml) for SBML's\nown nested-element structure, and (curry symbolic) to turn a kinetic\nlaw's embedded MathML into a real, inspectable symbolic expression\nrather than an opaque string. Scoped explicitly to species/reactions/\nstoichiometry plus mass-action/Michaelis-Menten kinetics as a bounded\nuseful first cut, not full MathML/SBML-package coverage.\n\nCross-links with docs/thoughts/toy-evolution-model.md (fixed that\nfiles own forward reference to this file, which did not exist yet\nwhen it was written).\n\nPre-implementation design doc; the base module itself is being\nimplemented in this same session.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(gillespie): add (curry gillespie) stochastic cell-biochemistry module\n\nBase capability from the design doc committed earlier this session:\nthe Gillespie stochastic simulation algorithm (SSA) for a set of\nchemical species undergoing reactions as a continuous-time Markov\nchain, with composable rate-law combinators (mass-action, arrhenius,\nmichaelis-menten, hill, rate*) as the whole environment-sensitivity\nmechanism -- temperature/pH/nutrient dependence is never a special\nfeature, just a propensity procedure reading environment fields,\ncomposed with ordinary function composition rather than a mini-DSL.\n\nTwo rounds of independent code review found five real bugs before\nthis landed, in order:\n\nRound 1 (also caught manually via direct runtime testing, same\npattern as the piper module earlier in this session -- toy/simulation\ncode needs to actually be run, not just read, to catch this class of\nbug):\n- %falling-factorial's n<=0 early-exit returned the unmultiplied\n  accumulator (1) instead of the correct 0 for a depleted reactant,\n  so mass-action kept a nonzero propensity after a reactant hit zero\n  -- a real simulation run consumed a finite resource down to -970\n  instead of stopping at 0.\n- michaelis-menten's km+S=0 case divided 0 by 0 unguarded -- a silent\n  NaN that poisons gillespie-step!'s (> a0 0) quiescence check\n  forever, permanently freezing an otherwise-live cell with no error.\n\nRound 2 (code review only, none of these were hit by manual testing):\n- cell-trajectory hung forever for dt <= 0 (next-sample never advanced\n  past t-max).\n- cell-trajectory accumulated sample times via repeated floating-point\n  addition of dt, which drifts for a dt that isn't an exact binary\n  fraction (e.g. 0.1) and can shift the final sample across the t-max\n  boundary -- switched to i*dt from an integer step count instead.\n- cell-trajectory kept rebuilding an identical species snapshot via\n  hash-table->alist at every remaining sample point even after the\n  cell had gone quiescent -- now caches and reuses one snapshot.\n- random-real() can return exactly 0.0 (probability 2^-53 per draw,\n  not zero), making (log 0.0) = -inf.0 and the drawn waiting time\n  +inf.0 -- silently jumping the cell's clock to infinity in one step\n  and ending the run early with no error. Fixed with a redraw-on-exact-\n  zero wrapper, effectively free amortized given the probability.\n- arrhenius/hill lacked the same zero-denominator guard michaelis-\n  menten already had (temperature=0, width=0 respectively) -- both now\n  return their correct mathematical limit (0, and a delta function)\n  instead of dividing by zero.\n\nAlso: docs/reference/module-gillespie.md (full API reference),\ndocs/guides/gillespie-cells.md (narrative walkthrough building up a\nbirth-death process, a genetic toggle switch, temperature sensitivity,\nand multi-cell simulation via curry's existing actors -- every code\nexample in it independently verified to actually run), a cookbook\nchapter in docs/thoughts/anarchists-cookbook.md, and an index entry in\ndocs/reference/modules.md and the README guide list.\n\n44/44 gillespie-specific tests pass (including regression tests for\nall five bugs above), 103/103 ctest suites pass overall (fresh\n--clear-cache run). Design doc (docs/thoughts/gillespie-cell-model.md,\ncommitted earlier) also covers an SBML-import idea and composing this\nwith a toy population-genetics model, neither implemented here.\n\nI thank Claude for the help with exploring ideas and, of course with the development\n\nIn case anyone is worried - I may use AI, but I remain responsible for its direction and its output.",
          "timestamp": "2026-08-27T19:57:30+10:00",
          "tree_id": "7734fd12f6f5376ebe561245d9c3eb77d9eaa5ad",
          "url": "https://github.com/deconstructo/curry/commit/26ae094d6b607efeb9d9c5d2f896f927f0f3c56c"
        },
        "date": 1787824689647,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.395,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.5,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.629,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.221,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 139.322,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 288.07,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.427,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 90.114,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.135,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "704a4abcb904d8a32f9b63e75e139cdc99b575da",
          "message": "feat(srfi): close Tier 1 gaps in SRFI-125/128/170/227 (#79)\n\n* feat(srfi170): add owner/unchanged, group/unchanged, user-info:parsed-full-name\n\nCloses a silent gap found by a full 39-SRFI audit: SRFI-170 defines\nowner/unchanged and group/unchanged (pass either to set-file-owner to\nleave that half alone) and user-info:parsed-full-name (the GECOS\nfull-name portion up to the first comma), none of which curry\nexported.\n\nowner/unchanged and group/unchanged are both -1, the same sentinel\nchown(2) itself already treats as \"don't change this id\" -- set-file-\nowner's C implementation (fn_set_file_owner, modules/posix/posix.c)\nalready passes uid/gid straight through with no translation, so no\nC-side change was needed for the constants themselves. Added a\ncomment there anyway flagging the now-invisible dependency, so a\nfuture defensive-hardening change to that function (e.g. rejecting\nnegative uid/gid) doesn't silently break these two Scheme-level\nconstants with no signal pointing back to why.\n\nuser-info:parsed-full-name assumes the same office/work-phone/home-\nphone GECOS convention SRFI-170's own \"parsed\" variant is defined\nagainst; documented the (rare, undetectable-from-the-string-alone)\nalternate \"Lastname, Firstname\" convention some systems use instead,\nper independent code review.\n\nAdded to both the numeric shim (170.scm) and the legacy dashed-name\nshim (srfi-170.scm) -- the latter was initially missed entirely (code\nreview caught it): the two shims are separate define-library forms\neach with their own explicit export list, so adding a name to one\ndoes not make it visible through the other.\n\nNew test file tests/srfi_170_tests.scm (no prior dedicated SRFI-170\ntest file existed). 105/105 ctest suites pass overall (fresh\n--clear-cache run). Independent code review; findings addressed.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(srfi125): add hash-table-mutable?\n\nCloses a silent gap found by a full 39-SRFI audit. curry has no\nimmutable hash tables, same reason SRFI-126's own hashtable-mutable?\n(s126/hashtables.scm) is already a hardcoded #t -- this is the\nmatching SRFI-125-named procedure for the same fact.\n\nAdded to both the numeric shim (125.scm) and the legacy dashed-name\nshim (srfi-125.scm); the latter was initially missed (see the SRFI-170\ncommit's own note on why -- each shim is a separate define-library\nwith its own explicit export list).\n\n105/105 ctest suites pass overall (fresh --clear-cache run).\nIndependent code review; no findings against this file specifically.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(srfi128): add make-eq-comparator, make-eqv-comparator, make-equal-comparator\n\nCloses a silent gap found by a full 39-SRFI audit. SRFI-128 defines\nboth ready-made comparator values (eq-comparator/eqv-comparator/\nequal-comparator, already present) and zero-argument typed\nconstructors that return the same objects -- curry only had the\nformer. Each new constructor just returns the existing matching\nvalue.\n\nAdded to both the numeric shim (128.scm) and the legacy dashed-name\nshim (srfi-128.scm); the latter was initially missed (see the SRFI-170\ncommit's own note on why).\n\n105/105 ctest suites pass overall (fresh --clear-cache run).\nIndependent code review; no findings against this file specifically.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(srfi227): add opt*-lambda, define-optionals, define-optionals*\n\nCloses a silent gap found by a full 39-SRFI audit -- half the spec's\nforms were missing.\n\nopt*-lambda is a pure forwarding macro to opt-lambda, not an\nindependent implementation: %opt-bind-optional (this file) is already\nsequential/let*-like (a later default-expr can see an earlier\noptional's bound value), the same simplification let-optionals/\nlet-optionals* below already make for the same reason -- curry's\nopt-lambda never had the spec's let-like (parallel) semantics to begin\nwith, pre-dating this addition. Since opt*-lambda contains no logic of\nits own, it can't drift out of sync with opt-lambda; if opt-lambda's\nbehavior is ever made genuinely parallel per spec, opt*-lambda needs\nno change, only a new implementation for opt-lambda to forward from\ninstead. Documented this reasoning inline per independent code review.\n\ndefine-optionals/define-optionals* are the definition-form sugar the\nspec builds on top of opt-lambda/opt*-lambda, the same relationship\nordinary (define (name . formals) body ...) has to (define name\n(lambda formals body ...)).\n\nAdded to both the numeric shim (227.scm) and the legacy dashed-name\nshim (srfi-227.scm); the latter was initially missed (see the SRFI-170\ncommit's own note on why).\n\n105/105 ctest suites pass overall (fresh --clear-cache run).\nIndependent code review; findings addressed.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* test(srfi): regression coverage for legacy dashed-name shims (srfi-125/128/170/227)\n\nIndependent code review found the four preceding commits' new exports\nhad only been added to each SRFI's numeric-named shim (e.g. 170.scm),\nnot its legacy dashed-name counterpart (srfi-170.scm) -- each is a\nseparate define-library form with its own explicit export list, so\n(import (srfi srfi-170)) then calling user-info:parsed-full-name\nraised unbound-variable while (import (srfi 170)) worked fine for the\nexact same call. All four dashed-name shims were fixed in their\nrespective commits; this file exists so that class of gap (fixing one\nnaming convention's shim, missing its sibling) can't reappear silently\nfor any of the four again.\n\nRegistration (tests/CMakeLists.txt) landed already in the SRFI-170\ncommit alongside srfi_170's own registration -- this commit is just\nthe test file itself.\n\n105/105 ctest suites pass overall (fresh --clear-cache run).\n\nCleaning up my poor code husbandry",
          "timestamp": "2026-08-27T20:49:25+10:00",
          "tree_id": "52622aaf6b8df02dfa8d03fa5a56cabe686daa2f",
          "url": "https://github.com/deconstructo/curry/commit/704a4abcb904d8a32f9b63e75e139cdc99b575da"
        },
        "date": 1787827808872,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.067,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.555,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.768,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.314,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 133.833,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 300.257,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.552,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 91.995,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.424,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "10ad93399e7f68c46dd2bbff480e25b981859c48",
          "message": "feat(srfi): close Tier 2 gaps in SRFI-1/27/125/128/133 (#80)\n\n* feat(srfi1): add Tier 2 gap-closing list procedures\n\nCloses Tier-2-effort gaps found by the earlier full 39-SRFI audit:\ncar+cdr, pair-fold, pair-fold-right, map-in-order, filter!, remove!,\npartition!, length+, except-last-pair, except-last-pair!,\nlset-diff+intersection, lset-diff+intersection!.\n\nTwo worth calling out specifically:\n\n- map-in-order is a real sequential loop, not an alias to map: this\n  file's own map/map! are re-exports of curry's core global map, which\n  goes parallel above map_par_threshold (src/builtins_curry.c) and\n  therefore does not guarantee left-to-right side-effect ordering.\n  map-in-order exists specifically for callers that need that\n  guarantee.\n\n- pair-fold-right avoids the reference implementation's natural non-\n  tail recursion (the same stack-overflow risk this file's own take/\n  unfold/take-while comments already document and avoid): walks left-\n  to-right collecting each pair via cons (which reverses the order),\n  then a single ordinary left-to-right fold over that already-reversed\n  list computes the same right-to-left result.\n\nIndependent review: the primary code-review agent hit its monthly\nspend limit mid-run (this has happened before this session); fell\nback to a careful manual read per established practice, focused on\nthe two trickiest additions (pair-fold-right's fold-based reversal\ntrick, lset-diff+intersection's single-partition derivation) plus\nlive verification of every new procedure's actual output against\nhand-computed expected values.\n\nAdded to both the numeric shim (1.scm) and the legacy dashed-name\nshim (srfi-1.scm), per the lesson from the Tier 1 PR (each is a\nseparate define-library with its own explicit export list).\n\n105/105 ctest suites pass overall (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(srfi27): add random-source-state-ref / random-source-state-set!\n\nCloses a Tier-2-effort gap found by the earlier full 39-SRFI audit.\nSRFI 27 leaves the state object's representation entirely up to the\nimplementation (opaque, only meant to round-trip through state-set!);\nrepresented here as a 4-element list of exact integers, one per\nxoshiro256+ state word.\n\nEach word is reinterpreted as a signed int64 for num_make_bignum_i\n(which itself falls back to a plain fixnum when the value fits) and\ndecoded back the same way via num_to_long, so the round-trip preserves\nthe exact original bit pattern regardless of whether a given word's\ntop bit happens to be set. Both new primitives take rng_mutex around\ntheir access to the shared RNG state, consistent with the mutex\ndiscipline added for this same state in the earlier RNG-detour PR.\n\nAdded to both the numeric shim (27.scm) and the legacy dashed-name\nshim (srfi-27.scm), per the lesson from the Tier 1 PR.\n\nIndependent review: the primary code-review agent hit its monthly\nspend limit mid-run; fell back to a careful manual read of this C\ndiff specifically (mutex discipline, bit-pattern round-trip\ncorrectness) plus live verification that a captured-then-restored\nstate produces an identical subsequent draw sequence.\n\n105/105 ctest suites pass overall (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(srfi125): add hash-table=?, hash-table-find, hash-table-pop!, hash-table-xor!\n\nCloses Tier-2-effort gaps found by the earlier full 39-SRFI audit.\n\nhash-table=? checks size equality plus every t1 entry having a\nmatching (per the given value-comparator's own equality predicate,\nnot necessarily equal? or either table's key-equality) entry in t2 --\nsize-equality plus that one-directional subset check together imply\nfull equality, since t2 can't hold any extra key once its size is\nalready accounted for by t1's own keys.\n\nhash-table-xor! (symmetric difference of keys, in place on t1) was\nchecked against the \"xor a table with itself\" edge case by hand:\nevery key starts present in t1 (since t1 and t2 are the same\nreference), so every key gets deleted during iteration and the table\nends up empty -- correct, matches the mathematical symmetric-\ndifference-of-a-set-with-itself result.\n\nAdded to both the numeric shim (125.scm) and the legacy dashed-name\nshim (srfi-125.scm), per the lesson from the Tier 1 PR.\n\nIndependent review: the primary code-review agent hit its monthly\nspend limit mid-run; fell back to a careful manual read of this file,\nplus live verification of all four procedures against hand-computed\nexpected values including the self-xor edge case above.\n\n105/105 ctest suites pass overall (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(srfi128): add standalone hash procedures, hash-bound, hash-salt, comparator-if<=>\n\nCloses Tier-2-effort gaps found by the earlier full 39-SRFI audit:\nboolean-hash, char-hash, char-ci-hash, string-hash, string-ci-hash,\nsymbol-hash, number-hash, default-hash, hash-bound, hash-salt,\ncomparator-if<=>.\n\nEvery hash procedure delegates to the existing %default-hash (the\nwrite-then-hash approach the built-in comparators already use\ninternally), except the two case-insensitive ones, which fold case\nfirst so char-ci=?/string-ci=?-equal values hash the same.\nhash-bound is %default-hash's own fixed modulus; hash-salt is a fixed,\nnon-secret constant since none of curry's built-in hash functions\nactually incorporate a salt (the spec requires hash-salt to exist and\nbe stable, not that every hash function read it).\n\ncomparator-if<=> supports both the 5-arg (default-comparator implied)\nand 6-arg (explicit comparator) forms via ordinary syntax-rules arity\nmatching. Caught and fixed one real bug while testing this by hand:\nthe 5-arg clause initially called (default-comparator) as if it were\na procedure, when it's actually a plain value (make-default-comparator\nis the procedure) -- found immediately via direct execution, not by\nthe (spend-limit-interrupted) review agent.\n\nAdded to both the numeric shim (128.scm) and the legacy dashed-name\nshim (srfi-128.scm), per the lesson from the Tier 1 PR.\n\nIndependent review: the primary code-review agent hit its monthly\nspend limit mid-run; fell back to a careful manual read of this file\nplus live verification of every new procedure and both\ncomparator-if<=> call forms.\n\n105/105 ctest suites pass overall (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(srfi133): add Tier 2 gap-closing vector procedures\n\nCloses Tier-2-effort gaps found by the earlier full 39-SRFI audit:\nvector-reverse-copy, vector-append-subvectors, vector-map!,\nvector-cumulate, vector-skip, vector-skip-right, vector-partition,\nreverse-vector->list, reverse-list->vector, vector-unfold!,\nvector-unfold-right!.\n\nvector-append-subvectors does two passes (sum the sub-range lengths,\nthen a single allocation plus vector-copy! per region) rather than\nbuilding an intermediate vector-copy per input and vector-appending\nthose. vector-partition returns two values (a new vector with\npred-matching elements first, in original relative order, then\nnon-matching, plus the match count, which also doubles as the\nboundary index between the two groups) via a single pass with two\nwrite cursors.\n\nvector-map!'s own comment initially claimed it matches curry's native\nvector-map's behavior on mismatched-length argument vectors -- checked\ndirectly by hand and found that claim false (native vector-map\nraises on a length mismatch; vector-map! here follows SRFI-133's own\ndocumented \"stop at the shortest vector\" convention instead, which is\na real, deliberate divergence, not a bug). Comment corrected to say so\nexplicitly rather than asserting a consistency that doesn't hold.\n\nAdded to both the numeric shim (133.scm) and the legacy dashed-name\nshim (srfi-133.scm), per the lesson from the Tier 1 PR.\n\nIndependent review: the primary code-review agent hit its monthly\nspend limit mid-run; fell back to a careful manual read of this file\n(including the vector-map! comment-accuracy check above) plus live\nverification of every new procedure against hand-computed expected\nvalues.\n\n105/105 ctest suites pass overall (fresh --clear-cache run). This is\nthe final commit of the Tier 2 SRFI gap-closing batch (SRFI-1, 27,\n125/126, 128, 133).\n\nTier 2 fixes to short cut implementations of SRFIs.\n\nAll hard work done by Claude. All errors, crap code, or missing functionality falls to me - as it should in this new age of development",
          "timestamp": "2026-08-28T14:17:57+10:00",
          "tree_id": "6b135857d5be35ee373550506ef5b3692a8ab429",
          "url": "https://github.com/deconstructo/curry/commit/10ad93399e7f68c46dd2bbff480e25b981859c48"
        },
        "date": 1787890718613,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.341,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 32.415,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.635,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.458,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 129.983,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 285.299,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.314,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 86.648,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.424,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "1be8be220b3c2fbcaae168be4b0c7a4033193514",
          "message": "Add AI and development section to README\n\nAdded a section explaining a little bit of the philosophy behind how AI is used. Also a call for people to get involved",
          "timestamp": "2026-08-28T14:38:26+10:00",
          "tree_id": "e04b1e315099e71e82a0a1cafc6f5e47cb5eccf3",
          "url": "https://github.com/deconstructo/curry/commit/1be8be220b3c2fbcaae168be4b0c7a4033193514"
        },
        "date": 1787891941741,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.927,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 26.013,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.199,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.584,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 139.341,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 285.245,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.573,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 91.785,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 70.826,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "988ae262fe63f198c8efb77295506e6c0691786c",
          "message": "Update AI and development section in README\n\nClarify responsibility for bugs and documentation errors.",
          "timestamp": "2026-08-28T14:49:21+10:00",
          "tree_id": "eb669a5069e6e7caf9d0bbf627bc3d6932f6c93f",
          "url": "https://github.com/deconstructo/curry/commit/988ae262fe63f198c8efb77295506e6c0691786c"
        },
        "date": 1787892599091,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.165,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.367,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.636,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.055,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 129.964,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 282.812,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.154,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 86.134,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.575,
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
          "id": "a65397c483e367ead377f86064060dbabe942ca0",
          "message": "chore(formula): update sha256 for v1.23.3 tarball\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-28T15:03:57+10:00",
          "tree_id": "d76611fd39c0ea803ec17f9d493c9a32403112d3",
          "url": "https://github.com/deconstructo/curry/commit/a65397c483e367ead377f86064060dbabe942ca0"
        },
        "date": 1787893537053,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.954,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.302,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.084,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.495,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 133.547,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 289.258,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.487,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 93.659,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 71.217,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "b72d291c1bb9ed3700b50283d5cc88a13e9d58ce",
          "message": "Update README to reference FEATURES.md for details\n\nClarified the purpose of README.md by emphasizing that features are detailed in FEATURES.md.",
          "timestamp": "2026-08-28T15:06:43+10:00",
          "tree_id": "4184b7654542bd7516af90a8f68c8062f2538484",
          "url": "https://github.com/deconstructo/curry/commit/b72d291c1bb9ed3700b50283d5cc88a13e9d58ce"
        },
        "date": 1787893659206,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.24,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.051,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.571,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.652,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 127.65,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 289.415,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.016,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.252,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.922,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "2cd32410a9d708eca7360ebd971a0dd4b785b48c",
          "message": "Restructured document \n\nRestructured document - moved philosophy and invitation to the bottom of the readme",
          "timestamp": "2026-08-28T15:08:40+10:00",
          "tree_id": "f9615ab3fbf8d2c607c52d2169dc289277a5a741",
          "url": "https://github.com/deconstructo/curry/commit/2cd32410a9d708eca7360ebd971a0dd4b785b48c"
        },
        "date": 1787893807051,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 10.662,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.14,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 2.827,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 24.55,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 76.043,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 199.514,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 48.965,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 54.594,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 40.091,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "a5a75a434bedab0ce1dbed12507b844f242d1e85",
          "message": "feat(srfi): add SRFI-9 (records), SRFI-31 (rec), SRFI-45 (lazy) (#82)\n\nThree of the four SRFI gaps assessed as free/trivial in the last audit\nround (the fourth, SRFI-61, turned out to require compiler changes --\nsee issue #81 and below).\n\n- SRFI-9 (Defining Record Types): thin re-export of curry's own core\n  define-record-type special form, which is already a strict superset\n  of SRFI-9's grammar (mutators, partial constructors). No new logic.\n\n- SRFI-31 (rec): a syntax-rules macro for self-referential expressions\n  (most commonly a recursive lambda) without a surrounding define or\n  letrec, transcribed from the SRFI's own reference implementation.\n\n- SRFI-45 (Primitives for Iterative Lazy Algorithms): curry's core\n  delay-force/make-promise already implement SRFI-45's constant-stack\n  lazy-forcing semantics -- verified by reading prim_force's iterative\n  trampoline in src/builtins.c, not just by a passing test. This shim\n  only needs to supply the two additional names, lazy (alias for\n  delay-force) and eager (alias for make-promise).\n\nEach ships the full three-file shape (srfi sN name, bare-numbered\nsrfi N, dashed srfi srfi-N), per the SRFI-8 template and the lesson\nfrom a prior review that fixes touching only the sN library silently\nmiss the two re-export shims. tests/srfi_9_31_45_tests.scm exercises\nall three library paths per SRFI; tests/srfi_legacy_dashed_names_tests.scm\ngained coverage for the three new dashed shims specifically as a\nregression guard against that same class of gap. 106/106 ctest suites\npass (fresh --clear-cache run on this branch, rebuilt standalone).\n\nIndependent code review (fresh subagent, no shared context) checked\nexport-list consistency across all three tiers, verified the rec macro\nagainst the SRFI-31 spec, and verified the lazy/eager semantic mapping\nby reading curry's actual C implementation rather than trusting the\ntest suite alone. No findings.\n\nSRFI-61 (a more general cond clause) was dropped from this batch: it\nrequires cond itself to accept a new clause shape, but curry's cond is\na hardcoded special form dispatched directly by the evaluator/compiler,\nnot resolved through macro/environment lookup, so a define-syntax cond\nshim is silently ignored (confirmed with a minimal repro). Filed as\nissue #81 for future compiler-level work.\n\nREADME.md/FEATURES.md's SRFI count bumped 39 -> 42.\n\nThere are some performance problems noted in the CI  - they are likely noise",
          "timestamp": "2026-08-28T15:46:29+10:00",
          "tree_id": "284c91ac55798e05a82eb60b8d80a851c766b39e",
          "url": "https://github.com/deconstructo/curry/commit/a5a75a434bedab0ce1dbed12507b844f242d1e85"
        },
        "date": 1787896069910,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.493,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 24.161,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.849,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.441,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 104.991,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 260.083,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 61.613,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 71.541,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 55.417,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "10f8527100a1669cba2762f41fda01d774f3f2e3",
          "message": "feat(websocket,ros): add (curry websocket) and (curry ros) modules (#85)\n\n* fix(port): render bytevector contents in write/display\n\nEvery bytevector (#u8(...) literals, (bytevector ...), sha1/base64-decode\noutput, etc.) printed as the generic #<object 11> fallback instead of its\nactual bytes -- the writer in src/port.c had a case for every other\ncompound heap type (pairs, vectors, closures, f64vectors, SRFI-4 typed\nvectors, tuples, conditions, ...) but never checked vis_bytes(v) before\nfalling through. Prints as #u8(...) now, matching R7RS's own external\nrepresentation and the existing #f64(.../typed-vector printers' shape.\n\nFound via direct interactive testing while implementing (curry websocket)\n(frame masking/handshake work needs bytevector arithmetic throughout, and\nprinting them while debugging just showed #<object 11>). Filed as #83.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(builtins): append raises a type error instead of segfaulting\n\nscm_append_inner only checked vis_nil(a) before unconditionally calling\nvcar(a)/vcdr(a) in its recursion. A non-pair, non-nil argument (e.g. a\nclosure) made vcar/vcdr read through it as if it were a Pair* regardless,\nwalking off into unrelated struct fields with no termination check --\neventually segfaulting the whole process instead of raising a catchable\nScheme error.\n\nTrivially triggered by an easy, unrelated mistake: curry's core\nhash-table-ref takes a plain default *value* as its third argument, not\na failure thunk like R7RS/SRFI-69 convention would suggest (the SRFI-69\ncompatibility shim already documents wrapping the core one specifically\nto \"correct\" this) -- passing a thunk there and later append-ing the\n(uncalled) result is a natural mistake that silently corrupts the\nprocess instead of erroring. Found exactly this way while implementing\n(curry ros)'s subscription bookkeeping. Filed as #84.\n\nFix adds a vis_pair(a) check before the recursive vcar/vcdr call,\nraising \"append: not a list\" -- matching the \"name: message\" style of\nsibling primitives (prim_list_tail/prim_list_ref) in the same file.\nZero-arg, one-arg, and multi-arg append with proper lists all still\nbehave identically (verified: the check can never fire on '() or a\npair, and the final argument is still permitted to be anything,\nincluding improper, matching R7RS and pre-fix behavior).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* docs(claude): record bug-report-as-issue convention\n\nPrompted by finding two real core bugs (bytevector print, append\nsegfault) while implementing an unrelated feature this session -- filing\nthem as GitHub issues in addition to fixing them keeps a durable,\nsearchable record independent of any one session's chat history.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* feat(websocket,ros): add (curry websocket) RFC 6455 client and (curry ros) rosbridge client\n\nLets curry talk to a running ROS1/ROS2 system through rosbridge_server's\nJSON protocol -- topics (publish/subscribe) and services (call/advertise)\n-- with no ROS client library, DDS transport, or ROS install linked into\ncurry itself. Both modules are pure Scheme, no new C code:\n\n- (curry websocket): a plain (ws://, no TLS) RFC 6455 client built on\n  curry's existing SRFI-106 sockets and (curry crypto)'s sha1/base64 for\n  the opening handshake. Handles frame masking, fragmentation\n  reassembly, transparent ping/pong, and the close handshake. A\n  per-connection mutex serializes the multi-part frame write since\n  multiple actors may call ws-send!/ws-close! on one connection\n  concurrently (the exact pattern (curry ros) uses: one reader actor,\n  arbitrarily many publisher/caller actors). Caps accepted frame length\n  at 64MB to avoid an unbounded-allocation attempt from a single crafted\n  frame header claiming an absurd 64-bit extended length.\n\n- (curry ros): the rosbridge v2.0 JSON protocol on top of the websocket\n  client + (curry json) + (curry sync) + curry's actor system.\n  ros-connect spawns a background reader actor that decodes every\n  incoming frame and dispatches by its \"op\" field to topic subscribers,\n  blocked ros-call-service callers (matched by a generated id, woken via\n  a per-call mutex+condvar), or locally-advertised service handlers.\n  Blocking service calls correctly survive spurious condvar wakeups\n  (loop re-checks the done flag rather than treating one non-done\n  wakeup as terminal) and are woken with a clean failure result rather\n  than hanging forever if the connection drops mid-call or closes\n  before the request could even be sent.\n\nBoth modules are tested against from-scratch, independently-implemented\nmock servers (raw sockets + hand-rolled WS server framing / rosbridge\nJSON, deliberately not reusing the client's own encode/decode logic) to\nprove real wire compatibility rather than self-consistency:\ntests/websocket_tests.scm (9 checks: handshake, echo, ping/pong,\nfragmentation, binary, oversized-frame rejection, close) and\ntests/ros_tests.scm (9 checks: subscribe/publish/call_service/\nadvertise_service round trips both directions, plus the\nconnection-drops-mid-call regression). 108/108 ctest suites pass.\n\ndocs/guides/ros-robot.md works through a concrete tracked-robot example:\ndifferential-drive teleop over /cmd_vel via rosbridge, driving real\nGPIO/PWM motor control through (curry rpi).\n\nIndependent code review (fresh subagent, no shared context) found three\nreal issues, all fixed before this commit: no bound on WebSocket frame\nlength (added the 64MB cap + regression test), a spurious-wakeup bug in\nros-call-service's wait loop that could falsely report failure and\nsilently drop the real response (fixed with a proper re-check loop), and\nno wakeup of blocked no-timeout callers on connection close/EOF (fixed\nby broadcasting a failure result to all pending calls on close/EOF).\nVerifying the third fix surfaced a fourth, related bug not caught by the\nreview -- an uncaught exception from ws-send! when the connection closes\nconcurrently with a call already killed the calling actor outright,\nindependent of the wakeup-on-close fix -- fixed by resolving the call as\nfailed directly instead of letting the exception escape.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-28T17:30:17+10:00",
          "tree_id": "fa5dce1d73c7cb22ff142f3e3682765d0be04191",
          "url": "https://github.com/deconstructo/curry/commit/10f8527100a1669cba2762f41fda01d774f3f2e3"
        },
        "date": 1787902266932,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.323,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.5,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.806,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.504,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 130.173,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 286.025,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.263,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.224,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.573,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "84d9bc8655bb492619f906fdcfdb68c3fd0d674d",
          "message": "feat(srfi): add SRFI-95 (Sorting and Merging), SRFI-78 (Lightweight Testing), SRFI-212 (Aliases) (#86)\n\nThree more SRFI gaps assessed as free/trivial in the last audit round.\n\n- SRFI-95 (Sorting and Merging): sort/sort!/merge/merge!/sorted? over\n  lists and vectors, with (sequence less? [key]) argument order and an\n  optional per-element key extractor. A thin argument-reordering/\n  key-wrapping shim over (srfi s132 sorting)'s existing real merge-sort\n  implementation, not a second sort algorithm.\n\n- SRFI-78 (Lightweight Testing): the check macro plus mode-controlled\n  counters/reporting. Implemented against the actual reference\n  implementation's semantics (fetched from\n  https://srfi.schemers.org/srfi-78/check.scm), not just the prose spec\n  -- they diverge in a way that matters: 'off mode means the checked\n  expression is genuinely never evaluated (wrapped in a thunk that's\n  simply never called) and never counted, not just evaluated silently;\n  'report mode (the default) prints every check result, pass or fail,\n  not only failures. check-ec (the SRFI-42 comprehension variant) isn't\n  implemented -- curry has no SRFI-42.\n\n- SRFI-212 (Aliases): define-alias, implemented as plain (define new\n  old). Documented limitation: can't alias a macro or one of curry's\n  ~40 hardcoded special forms (cond, lambda, define-record-type, ...),\n  the same wall SRFI-61 hit (issue #81) -- curry's macro system is\n  syntax-rules only, with no portable way to detect at expansion time\n  whether `old` names a macro rather than a value.\n\nEach ships the full three-file shape (srfi sN name, bare-numbered\nsrfi N, dashed srfi srfi-N). tests/srfi_95_78_212_tests.scm exercises\nall three library paths per SRFI (15 checks); srfi_legacy_dashed_names_tests.scm\ngained coverage for the three new dashed shims. 109/109 ctest suites\npass (fresh --clear-cache run, rebuilt standalone on this branch).\n\nTwo real bugs found and fixed during this work, independent of the\nfinal review:\n- SRFI-78's exported %check-one!/%check-proc helper (referenced by the\n  check macro's expansion) was initially only added to the s78 library's\n  own export list, not the two re-export shims -- exactly the class of\n  gap tests/srfi_legacy_dashed_names_tests.scm exists to catch, and it\n  did.\n- That same regression test's own local `check` helper procedure\n  collided with SRFI-78's newly-imported `check` macro in the same flat\n  script scope, silently turning `(check (+ 1 1) => 2)` into a\n  3-argument procedure call with `=>` evaluated as a bare unbound\n  variable. Renamed the test's helper to `assert-equal`.\n\nIndependent code review (fresh subagent, no shared context) found one\nfurther real bug, fixed before this commit: check-report printed the\nfirst-failure detail block in 'summary mode, which the reference\nimplementation suppresses there (summary mode prints only the pass/fail\ncounts). Fixed by gating that block on mode being 'report/'report-failed,\nwith new check-report-specific regression tests added (output captured\nvia with-output-to-string, not just visual inspection).\n\nREADME.md/FEATURES.md's SRFI count bumped 42 -> 45.\n\nThanks Claude :-)",
          "timestamp": "2026-08-28T18:10:23+10:00",
          "tree_id": "810b76775e7b2caef4aea01ac498a459b89f0012",
          "url": "https://github.com/deconstructo/curry/commit/84d9bc8655bb492619f906fdcfdb68c3fd0d674d"
        },
        "date": 1787904659882,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 12.462,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.424,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.276,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 25.355,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 88.404,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 224.782,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 54.141,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 60.534,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 45.295,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "4bbc0454e2df0c297f548b679fb164d43ca78813",
          "message": "refactor(srfi): rename bare-numbered/dashed shims from .scm to .sld (#87)\n\n* refactor(srfi): rename bare-numbered/dashed shim files from .scm to .sld\n\nToward SRFI-97's own naming convention being properly reflected in the\nsource tree: the 89 loose files directly under lib/curry/modules/srfi/\n(srfi/N.scm, srfi/srfi-N.scm) are pure one-line re-export declarations\nwith zero implementation of their own -- they exist purely to import a\nreal (srfi sN name) library and re-export everything it exports. .sld\n(Scheme Library Definition) is the standard R7RS-ecosystem file type for\nexactly this: a manifest declaring a library's name/imports/exports,\ndistinct from the .scm files that hold actual implementation code.\ncurry's own module loader (src/modules.c) already tries .sld before .scm\nat a given library path, so this is a pure rename with zero resolver\nchanges and zero behavior change -- confirmed via full ctest run before\nand after (109/109 both times) and via a real `cmake --install` dry run.\n\nFixes a real packaging bug this rename would otherwise have introduced\nsilently: CMakeLists.txt's own module-install rule only matched\n`FILES_MATCHING PATTERN \"*.scm\"`, which would have dropped all 89\nrenamed .sld manifests from any packaged/installed build (Homebrew,\n.deb/.rpm, `cmake --install` in general) while leaving the dev tree\n(which runs directly from the source checkout) working fine and masking\nthe gap. Added `PATTERN \"*.sld\"` alongside the existing pattern; verified\nwith a real `cmake --install --prefix /tmp/...` that all 89 land in the\ninstalled tree.\n\ndocs/reference/srfi/index.md's own prose updated to say .sld, not .scm,\nfor these shims.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* docs(writing-a-module): document the multi-file .sld+.scm module pattern\n\nAnswers the natural follow-up to the SRFI shim .sld rename: (curry X)\nmodules can already be split across multiple files today, no new\ncapability needed -- R7RS's own (include \"filename\") library\ndeclaration already works in curry, resolves relative to the including\nfile's own directory (fixed for exactly this during the SRFI-279 port,\nsee tests/fixtures/include_relative/), and is exactly the mechanism\nupstream SRFI reference implementations with per-platform files (e.g.\nSRFI-279's own chibi.scm/guile.scm/generic.scm split) already rely on.\n\nVerified end to end with a fresh throwaway module (.sld manifest with\ntwo (include ...) clauses pulling in two plain .scm files, one defining\na private helper the other's public procedure uses) before writing this\nup, not just asserted from reading the code.\n\nNo code changes -- this documents an existing capability, doesn't add\none. Whether/which existing (curry X) modules are worth splitting this\nway is a separate, later decision -- most modules are fine as a single\nfile and shouldn't be split just because the option now exists.",
          "timestamp": "2026-08-28T19:17:46+10:00",
          "tree_id": "1388493f34e4b4091e552a9866f14a9d8dfab7da",
          "url": "https://github.com/deconstructo/curry/commit/4bbc0454e2df0c297f548b679fb164d43ca78813"
        },
        "date": 1787908704140,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.474,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.921,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.957,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.866,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 129.074,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 289.715,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.527,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.482,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.743,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "7140ff04eb4a949201dc308707d0032d0944b81f",
          "message": "feat(srfi): add SRFI-141 (Integer Division) (#89)\n\nAdds the four division-operator families SRFI-141 defines beyond what\nR7RS already standardizes (floor and truncate are core R7RS; this adds\nceiling, round, euclidean, and balanced). Curry's own hash-table-ref\nassessment aside, this one was flagged as a genuine, currently-missing,\nlow-cost gap worth closing -- unlike SRFI-143 (fixnums) and SRFI-144\n(flonums), assessed separately and intentionally skipped: 143 exists\npurely to let a compiler skip type-dispatch overhead by guaranteeing a\nbounded machine integer, which fights curry's own numeric-tower design\ngoal of making that distinction invisible, and 144 is mostly a thin\n~80-procedure naming wrapper around math curry already has generically.\n\nEvery family here is derived from R7RS's own floor-quotient/\nfloor-remainder rather than raw division, ships as the standard\nthree-file shape (srfi s141 division-operators / srfi 141 / srfi\nsrfi-141, using the new .sld manifest convention), and is tested with\n27 checks including bignum cases.\n\nIndependent code review (fresh subagent, no shared context) ran\nexhaustive brute-force checks (d in [-8,8], n in [-30,30], 976\ncombinations per family) plus targeted bignum cases against a live\nbuild, not just static reading. ceiling/round/euclidean all verified\ncorrect across every sign combination. Found one real bug: balanced/'s\ntie-breaking rule only accounted for positive divisors (the file's own\nderivation comments only worked through positive-d examples by hand) --\nfor a negative divisor, a tie must resolve toward the *smaller*\nquotient, not the larger one, since the valid remainder range\n[-|d|/2, |d|/2) is asymmetric and which candidate remainder actually\nfalls in range flips with the sign of d. Fixed, with the exact failing\ncases from review (and a bignum negative-divisor tie) added as\nregression tests.\n\nReview also caught an overstated claim in the file's own header\ncomment: floor-quotient/floor-remainder's zero-denominator check is\ngenuinely inherited for free, but their non-integer-argument check is\nnot -- curry's core primitives silently accept a flonum or rational and\nreturn a plausible-but-wrong result rather than raising, a pre-existing\ngap this file's comment incorrectly claimed didn't apply. Comment\ncorrected; the underlying core gap filed as issue #88 rather than fixed\nhere, since it's in shared primitives well outside this SRFI's own\nscope.",
          "timestamp": "2026-08-28T23:43:49+10:00",
          "tree_id": "986f79a3ab262c2146938fdb0360353086a2afaa",
          "url": "https://github.com/deconstructo/curry/commit/7140ff04eb4a949201dc308707d0032d0944b81f"
        },
        "date": 1787924678668,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 12.625,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 21.945,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.693,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 27.964,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 96.036,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 245.698,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 59.443,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 66.775,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 49.289,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "975a4b9e7586c2837341422b698d3dcebf5c76ab",
          "message": "Simplify curry features overview\n\nRemoved references to build/install instructions and API surface links.",
          "timestamp": "2026-08-29T01:39:41+10:00",
          "tree_id": "06121eee190ed92036e810ff870bba7c89c04c25",
          "url": "https://github.com/deconstructo/curry/commit/975a4b9e7586c2837341422b698d3dcebf5c76ab"
        },
        "date": 1787931619766,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.933,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.389,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.127,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.089,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 131.854,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 287.862,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.189,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 90.825,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 67.965,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "f89808493a469e1abad802bea3f608963f3a648b",
          "message": "Remove Gillespie simulation section from FEATURES.md\n\nRemoved the section on the Gillespie stochastic simulation algorithm from the FEATURES.md file.",
          "timestamp": "2026-08-29T01:41:01+10:00",
          "tree_id": "247eb32cd9a14ef40ddf7cc4f45f1717e50f4ed4",
          "url": "https://github.com/deconstructo/curry/commit/f89808493a469e1abad802bea3f608963f3a648b"
        },
        "date": 1787931699341,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.917,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.702,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.1,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.247,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 139.558,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 284.345,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.203,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 90.359,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 69.295,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "8d1090dc52f8e3d42a8e9aa9ce2ef3766f506183",
          "message": "docs(roadmap): catch up through v1.23.3 plus unreleased work on main (#90)\n\nThe roadmap hadn't been touched since v1.16.0/v1.17.0, but CHANGELOG.md\nhad moved on through v1.23.3, and a further tranche (7 new SRFI\nlibraries, (curry websocket)/(curry ros), the .sld manifest convention,\ntwo core bugfixes) had already been merged to main without a version\nbump. Catches all of it up:\n\n- \"Where we are now\" table: compiler IR pipeline (the first real IR\n  curry has had, shipped v1.22.0-v1.23.0), TTS, (curry sql), (curry okf),\n  cond-expand/(features), Gillespie, websocket/ros, current SRFI count\n  (46), and an explicit ✗ row for full multi-shot call/cc (previously\n  not listed as a tracked gap at all).\n- Summary timeline: nine missing version rows (v1.17.0 through v1.23.3)\n  plus one row for the unreleased-but-merged tranche, each checked\n  against CHANGELOG.md's actual headlines rather than guessed -- caught\n  and fixed two misattributions while cross-checking (the SQL layer\n  landed in v1.18.0, not spread across v1.17.x; v1.19.0's own content\n  (mariadb/postgres backends) had been silently folded into a v1.20.0\n  entry and needed its own row).\n- New \"Active work outside the phase numbering\" section: the four\n  threads asked about directly -- performance (cross-referencing\n  performance-chez-kaappi.md's own Tier status, noting its \"no IR yet\"\n  claim is now stale since Tier 2 shipped after that doc's last\n  verification pass), complete multi-shot continuations (still blocked\n  on the same tree-walker-elimination prerequisite as the GC/performance\n  work), the GC rewrite (flagged as status-uncertain rather than\n  \"in progress\" -- the gc-rewrite/gc-perf branches this roadmap\n  previously asserted were active no longer exist anywhere in this\n  repository, confirmed by checking, not assumed), and SRFI compatibility\n  (audit history, the 7 libraries added since the audit, SRFI-61 blocked\n  on issue #81, SRFI-143/211 decided against with reasoning, SRFI-144/\n  146/275/13/41/115 parked as real follow-up work).\n- Corrected every other place in the document that still asserted the\n  gc-rewrite branch was actively in progress, now that its status is\n  established as uncertain rather than assumed continuing.\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-08-29T08:55:55+10:00",
          "tree_id": "95a4cd2143c9b1c1724b9947ab29fdaee6391d9e",
          "url": "https://github.com/deconstructo/curry/commit/8d1090dc52f8e3d42a8e9aa9ce2ef3766f506183"
        },
        "date": 1787957793167,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.95,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.48,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.328,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.207,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 176.841,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 288.432,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.045,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 108.915,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 73.02,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "95c537839887fbb249d754a4a432c1d161cd29d6",
          "message": "feat(srfi): add SRFI-41 (Streams) (#91)\n\nSRFI-41's own reference implementation has to build its own\nmemoizing lazy-promise machinery from scratch (it predates R7RS's\ndelay-force); curry already has delay/delay-force/force natively with\nexactly the iterative, stack-safe chain-flattening the SRFI's own spec\ncalls \"vitally critical\", so this port represents a stream AS a curry\npromise directly rather than wrapping one in a distinct record.\n\nThe cost of that representation, disclosed in both the module header\nand the doc page: stream? really means \"is this a promise\", not \"is\nthis specifically a stream\" -- (stream? (delay 5)) is #t here, #f in\nthe reference. Unavoidable while keeping the stack-safety property: a\ndistinct wrapper record would stop delay-force's own chain-flattening\nfrom seeing through it, which is the entire reason to build this on\ndelay-force at all.\n\n~43 bindings: the 8 SRFI-41 primitives plus ~35 derived procedures,\nported from the SRFI's own reference implementation where one exists\n(stream-unfold, stream-unfolds, stream-range, stream-scan, stream-take,\nstream-drop-while, stream-concat, and more, checked line-by-line against\nthe actual spec text during review) or derived directly where it doesn't\n(stream-append isn't in the SRFI's reference appendix; derived by\nmirroring stream-concat's own structure, since a plain list of stream\narguments rather than a stream-of-streams is the only structural\ndifference between the two).\n\nFound and fixed one real thing along the way: curry has no case-lambda\nat all (confirmed by an existing comment elsewhere in the codebase,\nlib/curry/modules/curry/schematic/format.scm) -- an initial draft of\nstream->list using it failed with a confusing \"unbound variable: x\"\ninside the clause body rather than a clean \"case-lambda not supported\"\nerror. Rewritten using the reference implementation's own plain\nrest-arg dispatch instead (which doesn't use case-lambda either).\n\nShips as the standard three-file shape. tests/srfi_41_tests.scm (40\nchecks) covers every derived procedure plus a dedicated stack-safety\nsection (100,000-element stream traversal, verifying the whole point of\nbuilding this on delay-force, not just correctness).",
          "timestamp": "2026-08-29T10:44:57+10:00",
          "tree_id": "48a30a5a9077089157e8ae15c08dc55a815396e7",
          "url": "https://github.com/deconstructo/curry/commit/95c537839887fbb249d754a4a432c1d161cd29d6"
        },
        "date": 1787964344117,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.227,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.061,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.729,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.67,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 127.192,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 294.371,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.818,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.752,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 63.529,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "68d4a304006df2064c387175c1e6895c1f5c3363",
          "message": "feat(websocket): add server role (ws-listen/ws-accept) to (curry websocket) (#92)\n\nAdds the missing half of RFC 6455 to the existing client-only module:\nws-listen (bind+listen, mirrors (curry network)'s tcp-listen), ws-accept\n(blocks for a connection, performs the server-side opening handshake,\nreturns a fully negotiated connection), ws-listener?, ws-listener-close!,\nand a ws-path accessor (the request path a server connection was opened\nagainst, for path-based routing).\n\nClient and server share every piece of frame-level machinery -- masking,\nlength encoding, fragmentation reassembly, ping/pong -- via one <ws>\nrecord now carrying a role field ('client or 'server). The only place\nrole matters is which direction masks (RFC 6455 5.1: client-to-server\nframes MUST be masked, server-to-client frames MUST NOT be).\n\nTested against an independent hand-rolled RFC 6455 CLIENT (deliberately\nnot reusing ws-connect or any of this module's own framing code) driving\nthe real ws-listen/ws-accept server -- the same wire-compatibility\nphilosophy websocket_tests.scm already established for the client role,\nnow applied to the server: proving genuine protocol compliance, not\nself-consistency. Verified the handshake against RFC 6455 section 1.3's\nown worked Sec-WebSocket-Key/Accept example, not just a self-generated\nkey/accept pair.\n\nIndependent code review (fresh subagent, no shared context), specifically\nasked to focus on security since this is now server code accepting\narbitrary untrusted network input rather than client code talking to a\nserver the caller already chose to trust, found two real issues, both\nfixed here:\n\n- %read-header-lines (shared by both roles) delegated to curry's core\n  read-line, which has no line-length cap at all -- confirmed live with\n  a 2MB unterminated header line still buffering with no error 0.5s\n  later. A single connection to ws-accept could drive unbounded memory\n  growth. Fixed with an explicit char-at-a-time reader (8KB per line,\n  100 lines per request) -- checking length after read-line returns\n  wouldn't have helped, since the unbounded blocking already happens\n  before control comes back.\n\n- %read-frame silently unmasked/accepted a frame regardless of which\n  direction it arrived from, correct for round-tripping against a\n  well-behaved peer (why every prior test passed) but not enforcing RFC\n  6455 5.1's actual MUST-reject requirement -- masking direction is a\n  real security control (defense against cache/protocol-confusion\n  attacks via naive intermediaries), not wire-format decoration. Both\n  directions now raise a clean protocol error and close the connection\n  on a violation instead of tolerating it.\n\nBoth fixes verified against the built binary (not just reasoned about)\nand locked in as permanent regression tests in both test files.\n\n- lets be able to handle web clients :-)",
          "timestamp": "2026-08-29T13:45:41+10:00",
          "tree_id": "6a187e206175952facd63715debf434c608bb20d",
          "url": "https://github.com/deconstructo/curry/commit/68d4a304006df2064c387175c1e6895c1f5c3363"
        },
        "date": 1787975184860,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.88,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.76,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.55,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.351,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 129.016,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 288.336,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.725,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.073,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 64.815,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "e6f387c1a1482c884db6030612745187aa1ffde2",
          "message": "Edit README to streamline development philosophy section (#93)\n\nRemoved personal reflections on coding speed and development philosophy.",
          "timestamp": "2026-08-29T13:48:05+10:00",
          "tree_id": "05dda2ee45dd68ef9d49af4a5fc720abd3e48240",
          "url": "https://github.com/deconstructo/curry/commit/e6f387c1a1482c884db6030612745187aa1ffde2"
        },
        "date": 1787975338721,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.27,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 28.908,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.561,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.002,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 129.693,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 290.714,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.106,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.66,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.2,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "a04e7b0cee1113afb4833778e87cb50a59f5675e",
          "message": "fix(builtins): floor-quotient/floor-remainder/floor/ reject non-integer arguments (#94)\n\nR7RS requires exact integer arguments for these three, and\nquotient/remainder/truncate-quotient/truncate-remainder already enforce\nthis correctly -- they route through numeric.c's to_mpz(), which raises\n\"exact integer required, got X\" on a rational, flonum, or complex\nargument. The floor family never went anywhere near that check: they're\nbuilt from num_floor(num_div(a,b)), plain real-number arithmetic that\n\"succeeds\" on any numeric type and silently returns a plausible-but-\nwrong result instead of rejecting it -- (floor-quotient 15/2 2) => 3,\n(floor-remainder 7.5 2) => 1.5.\n\nFixes #88, found during an earlier session's SRFI-141 (Integer\nDivision) implementation -- SRFI-141's own ceiling/round/euclidean/\nbalanced families are all built on floor-quotient/floor-remainder, so\nthe gap silently propagated (e.g. euclidean/ on a rational input\nreturned a wrong-but-plausible pair instead of raising).\n\nAdds check_exact_integer, matching to_mpz's exact classification order\n(fixnum/bignum accepted; else rational/inexact-real/complex/non-numeric),\ncalled at the top of all three primitives with each one's own correct\nprocedure name -- not just delegating to floor-quotient's internal call\nand letting the error misattribute to the wrong procedure, since\nfloor-remainder and floor/ both call floor-quotient as part of computing\ntheir own result.\n\nIndependent code review (fresh subagent, no shared context) verified\nlive: both exact repros from the issue, both bignums (confirmed correct\non 2^200-scale values), rejection on either argument position, and\ncomplex-number classification, plus confirmed no other unguarded call\nsite of the shared internal helper. 112/112 ctest suites pass; 4 new\nregression tests in tests/r7rs_tests.scm.",
          "timestamp": "2026-08-30T20:22:32+10:00",
          "tree_id": "e18c39ae8e8c30875d99ec4ba037f0cac8082008",
          "url": "https://github.com/deconstructo/curry/commit/a04e7b0cee1113afb4833778e87cb50a59f5675e"
        },
        "date": 1788085396987,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.127,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 19.456,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.013,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 23.04,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 104.91,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 219.558,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 50.136,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 70.114,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 53.693,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "12a1bc55851e2b8445c758ffe96f8602e7e4c0e5",
          "message": "docs(language): document core hash tables, including the ref-thunk gotcha (#95)\n\nCloses out the remaining thread on GH issue #84 -- the crash itself was\nalready fixed (append raises a clean error on a non-list argument\ninstead of segfaulting, PR #85), but the issue's \"Suggested fix\" section\nflagged a second, smaller question: whether there's a real naming-\nconsistency gap around hash-table-ref/default.",
          "timestamp": "2026-08-30T20:35:30+10:00",
          "tree_id": "e8899696482c3286770a855b3ffbf98a91d798ef",
          "url": "https://github.com/deconstructo/curry/commit/12a1bc55851e2b8445c758ffe96f8602e7e4c0e5"
        },
        "date": 1788086181651,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.012,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.196,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.608,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.195,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 131.864,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 294.447,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 69.945,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.704,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 67.53,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "e648e0f6caa2ec414ce89f975cf7135dbc5199d0",
          "message": "feat(introspection): disassemble/,asm, macroexpand/,expand, type-of, list-actors/,actors (#97)\n\n* feat(introspection): add disassemble/,asm, macroexpand/,expand, type-of, list-actors/,actors\n\nImplements the remaining REPL introspection tools from cill_spec.pdf\nSec 13.5 that curry did not already have (trace!/untrace! and break!/\nunbreak! were already covered by the existing trace/untrace builtins\nand ,break/,unbreak REPL commands).\n\n- (disassemble proc) / ,asm <name>: exposes chunk.c's pre-existing\n  internal disassembler (previously stderr-only, used by\n  compiler_ir_checks.c's codegen-comparison tool) as a Scheme builtin\n  and REPL command, via open_memstream.\n- (macro? sym), (macroexpand-1 form), (macroexpand form) / ,expand\n  <expr>: GLOBAL_ENV-only macro introspection built on the existing\n  T_SYNTAX/apply machinery.\n- (type-of x): maps ObjType/immediate tags to a symbol.\n- (list-actors) / ,actors: a new fixed-4096-slot global actor registry\n  in actors.c, populated on spawn and cleared on the actor's own exit.\n\nFound and fixed a real, independent bug in chunk.c's existing\ndisassembler while wiring it up: OP_CLOSURE's variable-length upvalue-\ntable encoding was never skipped, desyncing every later instruction in\nthe chunk for any closure with at least one upvalue. Filed as #96,\nfixed in this branch.\n\nIndependent review (two passes) found and this branch fixes two\nfurther real bugs before either shipped: actor_registry_add ran AFTER\npthread_create, letting a fast-exiting actor's own registry_remove run\nbefore the add and permanently stranding a dead entry (confirmed:\n368/500 short-lived actors leaked under the old ordering); and\nmacroexpand's next==expr fixpoint check never detects a self-\nreferential macro (each expansion step conses a fresh list), hanging\nforever -- fixed with a 1000-step cap that raises instead. Also fixed:\n,expand had no exception handler and could kill the whole REPL on a\nmalformed macro use; the OP_CLOSURE disassembler had no bounds check\non the upvalue-table read and silently misdecoded on a bad chunk\nconstant instead of aborting; a comment cited the wrong GC-rooting\nprecedent.\n\nAdds docs/thoughts/{profiling,concurrency,introspection}-uplift-plan.md\nand set-theory-synthesis-plan.md (written comparing curry's actual\nstate against cill_spec.pdf's design across several subsystems),\nlinked from docs/roadmap.md, plus documents the new builtins/REPL\ncommands in CLAUDE.md.\n\n113/113 ctest suites pass (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(repl): harden ,expand/,asm/,break/,unbreak/,debug against malformed arguments\n\nSecond independent review pass (verifying the first pass's fixes) found\nthat ,expand and ,asm's own scm_read call for their argument runs with\nno exception handler installed, so a malformed s-expression (e.g. an\nimproper \"(1 . . 2)\") had nowhere to go and killed the whole REPL\nprocess instead of printing a read error and continuing. The same gap\nalready existed in ,break/,unbreak/,debug (not introduced by this\nbranch), inherited rather than new -- fixed all five together with a\nnew shared safe_read() helper matching the main read loop's own\nexisting exception-safety pattern.\n\nAlso from the same review pass:\n- ,expand restored current_handler before writing the expansion result,\n  not after -- a raise during that write would have had nowhere to go.\n  Moved the restore after the write.\n- actor_spawn's pthread_create-failure branch wrote a->alive without\n  a->lock, unlike actor_thread's own exit path for the same field.\n  Provably race-free at that point (the actor hasn't escaped to any\n  caller yet), but locked anyway to match the exit path's pattern and\n  remove any doubt for a future reader.\n- Attempted to de-duplicate prim_disassemble's String construction by\n  calling api.c's curry_make_string_n instead -- reverted after it broke\n  curry_test's link (that target doesn't link api.c). Left in-file,\n  matching scm_make_string's own established convention in the same\n  file for exactly this reason.\n- Documented the actor registry's 4096-slot cap in CLAUDE.md.\n\nFiled #98 for a related but out-of-scope pre-existing gap the same\nreview found: scc.c's read_chunk doesn't validate upval_count before\nthe VM and disassembler both trust it, which can produce a confusing\nGC warning on a corrupted (not necessarily malicious) .scc file rather\nthan a clean error. Not fixed here since it's a separate area\n(.scc deserialization) from this branch's own scope.\n\nNew regression coverage: tests/test_cli.sh drives the REPL over stdin\nwith a malformed argument to each of the five affected commands and\nconfirms the REPL survives and keeps processing subsequent input.\n\n113/113 ctest suites pass (fresh --clear-cache run), plus test_cli.sh's\nnew cases (74/74 in that file).\n\nPulled a lot of interesting thoughts from another language - Cill - back into Curr.",
          "timestamp": "2026-08-31T20:30:36+10:00",
          "tree_id": "2b5e8691f1f1e4912ea993bfed4e7d62858a5fbb",
          "url": "https://github.com/deconstructo/curry/commit/e648e0f6caa2ec414ce89f975cf7135dbc5199d0"
        },
        "date": 1788172274592,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.325,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.751,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.683,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.811,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 132.479,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 283.502,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.514,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.336,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.637,
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
          "id": "a240bc484f462db0435fa568dee4f4b697ee6cd3",
          "message": "Update Formula/curry.rb sha256 for v1.23.4",
          "timestamp": "2026-08-31T20:39:22+10:00",
          "tree_id": "96c7b4e733e90f34a03ea1f87ae9301cd42c649e",
          "url": "https://github.com/deconstructo/curry/commit/a240bc484f462db0435fa568dee4f4b697ee6cd3"
        },
        "date": 1788172826362,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 19.397,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 35.812,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.882,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 42.061,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 144.18,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 320.612,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 74.308,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 97.626,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 79.273,
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
          "id": "638b3ccc16e4ec6ce00978b8ab72ebff6ca5b44a",
          "message": "docs(changelog): add v1.23.4 entry",
          "timestamp": "2026-08-31T21:07:03+10:00",
          "tree_id": "94c721e0b6592a655da03a6e632c41e29b660990",
          "url": "https://github.com/deconstructo/curry/commit/638b3ccc16e4ec6ce00978b8ab72ebff6ca5b44a"
        },
        "date": 1788174463573,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.21,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.574,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.76,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.732,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 130.873,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 289.406,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 68.289,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.454,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 63.917,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "a6c7044744ec6bb89b3d4f5f08aad49ae165ae90",
          "message": "fix(build): _Thread_local is a C11 keyword, not C++ -- breaks LLVM backend build on GCC (#100)\n\nReported building on a Raspberry Pi with -DBUILD_LLVM=ON: g++ rejects\n\"extern _Thread_local VM *vm;\" in vm.h with \"'_Thread_local' does not\nname a type\" the moment src/llvm/*.cpp (C++) transitively includes it,\ncascading into \"'vm' was not declared in this scope\" everywhere it's\nused afterward.\n\n_Thread_local is C11-only; C++'s equivalent is the thread_local\nkeyword. Clang's C++ frontend tolerates _Thread_local as a\ncompatibility extension (why this never surfaced building on macOS),\nbut GCC's does not. Reproduced exactly against real GCC (Homebrew\ng++-16, not Apple's clang-in-disguise g++ symlink) before this fix,\nconfirmed resolved after.\n\nFix: new CURRY_THREAD_LOCAL macro in value.h (the one header every\naffected file already includes) expanding to thread_local under\n__cplusplus and _Thread_local otherwise. Replaced all 47 raw\n_Thread_local occurrences across headers and their corresponding .c\ndefinitions for consistency, even though the .c files themselves are\nnever compiled as C++ -- one spelling throughout, not two.\n\nAlso fixed a real header-ordering bug this surfaced: gc.h declared\nCURRY_THREAD_LOCAL-using externs (gc_nursery, etc.) at line 119 but\ndidn't include value.h (where the macro now lives) until line 242,\nfurther down the same file -- moved the include to the top, removing\nthe now-redundant later one. Every other affected header already\nincluded value.h before its own first thread-local use.\n\nVerified:\n- Real GCC (g++-16) repro of the Pi's exact error against unmodified\n  vm.h, then confirmed clean compilation of the same header after this\n  fix, using the actual reported error message as the test oracle.\n- Full LLVM backend build (-DBUILD_LLVM=ON, Clang) compiles and links\n  clean, including src/llvm/jit.cpp which was completely unreachable\n  as a build target on GCC before this fix.\n- 113/113 ctest suites pass (fresh --clear-cache run, LLVM off --\n  matches every other test run this session).\n\nFiled #99 for an unrelated, pre-existing issue found while verifying\nthis: this machine's current Homebrew llvm (23.1.0) crashes the JIT\ntier at runtime with an LLVM assertion (\"cannot get terminator of\nnon-well-formed block\") -- reproduces identically on unmodified main,\nunrelated to this fix, and out of scope here (needs someone bisecting\nthe actual JIT codegen against LLVM 15/18/23, not a build-portability\nfix).\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T03:33:22+10:00",
          "tree_id": "11d5b425664f63c406e6b2fed3edcdff9278de6e",
          "url": "https://github.com/deconstructo/curry/commit/a6c7044744ec6bb89b3d4f5f08aad49ae165ae90"
        },
        "date": 1788197643309,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.31,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.503,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.749,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.791,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 125.947,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 284.338,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.164,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.555,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.134,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "0e4891c551020a34502ae3f1174ad084ff8e0ac5",
          "message": "feat(scheme): implement (scheme case-lambda), fix phantom alias in modules.c (#104)\n\n`(scheme case-lambda)` was listed in modules.c's \"alias GLOBAL_ENV\"\ntable alongside the real (scheme base)/(scheme write)/etc. libraries,\nwhich meant (import (scheme case-lambda)) silently succeeded while\nproviding nothing at all -- GLOBAL_ENV never actually had a\ncase-lambda binding, so any code using it failed later with a\nconfusing unbound-variable error at the use site instead of a clean\n\"library not found\" at import time. Removed from that table; added a\nreal implementation at lib/curry/modules/scheme/case-lambda.sld (the\nstandard portable syntax-rules reference implementation).\n\nImplementing it surfaced two independent, pre-existing bugs in curry's\nsyntax-rules engine, both worked around in this file, filed separately\nsince fixing the engine itself is well beyond this scope:\n\n- #101: a pattern where an outer ... wraps a sub-pattern that itself\n  contains a variable followed by its own ... (e.g. \"(_ (a b ...) ...)\")\n  fails to bind the inner variable -- exactly the shape the reference\n  implementation's outer case-lambda macro normally uses. Worked\n  around by capturing each clause as one opaque pattern variable in\n  the outer macro and destructuring one clause at a time via ordinary\n  recursion in a helper macro instead.\n- Found by independent review, same category but distinct: curry's\n  syntax-rules does not rename template-introduced binders on\n  collision with a user identifier -- the macro's own args/len\n  bindings would silently shadow a same-named variable in a clause\n  body. Fixed by renaming to %cl-args/%cl-len.\n\nAlso from independent review: %case-lambda-help was working only by\nan accidental macro-export leak (non-exported macros are visible to\nimporters even though non-exported values aren't) rather than the\ndocumented mechanism -- now actually exported; the no-matching-clause\nerror now names the actual argument count that failed to match.\n\nFiled #102 (apply is not tail-called, so the canonical self-recursive\ncase-lambda accumulator idiom overflows the stack) and #103 (reader\nmisparses the improper `(. r)` literal as a 2-element list) as\nseparate, unrelated pre-existing issues found during review.\n\nCorrected several now-false \"curry has no case-lambda\" comments across\ndocs and lib/ files that predate this work and haven't been migrated\nto build on the real form (their own hand-rolled dispatch already\nworks and in most cases needs to stay hand-rolled regardless, e.g.\nSRFI-253's checked-formal fallthrough semantics that plain\ncase-lambda has no mechanism for).\n\n12 new regression tests (case_lambda_tests.scm), including both bugs\nfound by review. 114/114 ctest suites pass (fresh --clear-cache run).\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T04:37:44+10:00",
          "tree_id": "b84d02afeb5dd3f2e2fd8023263b2a762ac8d84f",
          "url": "https://github.com/deconstructo/curry/commit/0e4891c551020a34502ae3f1174ad084ff8e0ac5"
        },
        "date": 1788201516008,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.77,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.618,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.182,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.556,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 138.471,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 281.859,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.402,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.926,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.781,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "cc190357f079986f4c27e04077983b40dfafd45b",
          "message": "fix(llvm): use hasTerminator() instead of the asserting getTerminator() (#106)\n\nCloses #99.\n\nCompileCtx::terminated() tested for an unterminated basic block via\n'getTerminator() != nullptr'. LLVM's getTerminator() asserts the\nblock is ALREADY terminated before returning anything\n('assert(hasTerminator() && \"cannot get terminator of non-well-formed\nblock\")', BasicBlock.h) -- it was never designed to double as an\nis-it-terminated query. Apparently older LLVM releases didn't enforce\nthis (or shipped with assertions compiled out), letting this call\nsilently work by luck for years, until LLVM 23.1.0 (this machine's\ncurrent Homebrew version) hit the assertion at runtime the moment any\nJIT-compiled code exercised a control-flow path reaching an\nunterminated block -- which turned out to be nearly every\nsufficiently-looping test, cascading into 21 otherwise-unrelated\nctest targets (scheme_r7rs, actors, numeric_ext, sicm, syntax_rules,\nprofiling, posix, srfi_160, ...) all aborting the same way whenever\nthey ran long enough to cross the tiered JIT's promotion threshold.\n\nFixed with hasTerminator() -- LLVM's own plain, assertion-free bool\nquery for exactly this ('!empty() && back().isTerminator()') -- the\none-line change resolves the entire cascade, confirming it was all\nthe same root cause.\n\nVerified: standalone 'jit' ctest target now passes (previously failed\nimmediately); full ctest suite with -DBUILD_LLVM=ON is 115/115 (up\nfrom 94/115, with every previously-aborted target now passing);\nnon-LLVM build unaffected, 114/114.\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T07:53:41+10:00",
          "tree_id": "a69c11fb0ec4775ba16468c80389a859da8181bc",
          "url": "https://github.com/deconstructo/curry/commit/cc190357f079986f4c27e04077983b40dfafd45b"
        },
        "date": 1788213276796,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.454,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 33.957,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.698,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 38.953,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 126.115,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 287.77,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.599,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.205,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 64.178,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "12e37673b3918d19de25ce0d0986834cd9f9b1dc",
          "message": "fix(syntax-rules): correctly bind pattern variables under nested ellipsis (#107)\n\n* fix(llvm): use hasTerminator() instead of the asserting getTerminator()\n\nCloses #99.\n\nCompileCtx::terminated() tested for an unterminated basic block via\n'getTerminator() != nullptr'. LLVM's getTerminator() asserts the\nblock is ALREADY terminated before returning anything\n('assert(hasTerminator() && \"cannot get terminator of non-well-formed\nblock\")', BasicBlock.h) -- it was never designed to double as an\nis-it-terminated query. Apparently older LLVM releases didn't enforce\nthis (or shipped with assertions compiled out), letting this call\nsilently work by luck for years, until LLVM 23.1.0 (this machine's\ncurrent Homebrew version) hit the assertion at runtime the moment any\nJIT-compiled code exercised a control-flow path reaching an\nunterminated block -- which turned out to be nearly every\nsufficiently-looping test, cascading into 21 otherwise-unrelated\nctest targets (scheme_r7rs, actors, numeric_ext, sicm, syntax_rules,\nprofiling, posix, srfi_160, ...) all aborting the same way whenever\nthey ran long enough to cross the tiered JIT's promotion threshold.\n\nFixed with hasTerminator() -- LLVM's own plain, assertion-free bool\nquery for exactly this ('!empty() && back().isTerminator()') -- the\none-line change resolves the entire cascade, confirming it was all\nthe same root cause.\n\nVerified: standalone 'jit' ctest target now passes (previously failed\nimmediately); full ctest suite with -DBUILD_LLVM=ON is 115/115 (up\nfrom 94/115, with every previously-aborted target now passing);\nnon-LLVM build unaffected, 114/114.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(syntax-rules): correctly bind pattern variables under nested ellipsis\n\nCloses #101.\n\nA pattern where an outer ... wraps a sub-pattern that itself contains\na variable followed by its own ... (e.g. \"(a b ...) ...\") silently\nbound the inner variable to () instead of its actual matched values.\nMinimal repro:\n  (define-syntax my-test\n    (syntax-rules () ((_ (a b ...) ...) '((a b ...) ...))))\n  (my-test (1 10 20) (2 30))\n  ; was: ((1 () ()) (2 () ())), should be ((1 10 20) (2 30))\n\nRoot cause, match side (sr_match_list): per-group accumulation only\never searched sb (the inner match's scalar bindings) for each pattern\nvariable name, silently defaulting to () when a name's match actually\nlanded in se (the inner match's own ellipsis bindings -- exactly where\na variable with further ellipsis inside the outer sub-pattern lands)\ninstead. Root cause, expand side (sr_expand_list): the symmetric gap\non the way back out -- it never re-scoped ell_bindings per outer\niteration, so a nested \"name ...\" inside a repeated sub-template\niterated over the wrong (whole, cross-iteration) list rather than just\nthat iteration's own slice.\n\nFixed with a minimal, surgical change on both sides rather than a\nstructural rewrite: match side falls back to se when a name isn't\nfound in sb; expand side builds a per-iteration, shadowed ell_bindings\ntable before recursing. Both compose correctly to arbitrary nesting\ndepth by induction, since each level applies the exact same fallback\n-- verified directly with 3-level-deep nesting, asymmetric group\nsizes, a dotted tail inside a nested clause, and the (scheme\ncase-lambda) reference implementation's own direct (non-workaround)\nshape, all in the new regression suite.\n\n10 new regression tests (syntax_rules_nested_ellipsis_tests.scm).\nUpdated case-lambda.sld's comment, which described this as an open,\nunfixed engine limitation -- it isn't anymore, though the file's own\nopaque-clause workaround is left in place rather than reverted to the\nmore direct shape, since it's already correct and already tested.\n\n115/115 ctest suites pass (fresh --clear-cache run) -- including every\nexisting syntax-rules-dependent macro across every SRFI shim and\n(curry X) module in the codebase, with zero regressions, giving real\nconfidence this fix is safe despite touching code every macro in the\ncodebase depends on.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(syntax-rules): guard scm_list_ref against a malformed macro's SIGSEGV\n\nIndependent review of the nested-ellipsis fix (previous commit)\nfound that a malformed macro use mixing ellipsis depths in the same\nrepeated sub-template -- invalid per R7RS, but nothing in this file\nrejects it at match time -- crashes with an uncatchable SIGSEGV\n(macro expansion runs at compile time, not run time, so guard cannot\nhelp). The new per-iteration iter_ell_bindings shadowing broke an\ninvariant every OTHER ell_bindings value in this file relies on\n(that it's always a proper list): a depth-1 variable's shadowed\nper-iteration value is a scalar, and the unguarded scm_list_ref\ndereferenced it as a pair.\n\nSame root cause covers a second, actually pre-existing (not\nintroduced by the nested-ellipsis fix) crash the same review pass\nfound: two same-depth ellipsis variables of unequal length used\ntogether in one template hit the identical unguarded call.\n\nFixed both with the same guard at the one call site: a not-actually-\na-proper-list value degrades to V_NIL / zero repetitions for that\nposition instead of dereferencing garbage -- matching this\nmalformed-input case's actual pre-nested-ellipsis-fix behavior\n(wrong output, not a crash) rather than introducing a worse failure\nmode than before.\n\nAlso: fixed a stale comment in conditions.scm the same review found,\ndescribing the now-fixed nested-ellipsis limitation as still open.\n\n3 new regression tests proving the process survives all three\ncrash repros (mixed-depth, mixed-depth-with-dotted-tail,\nunequal-length-same-depth) -- correctness of the resulting output for\ngenuinely invalid macro input isn't a meaningful contract, only that\nexecution continues. 115/115 ctest suites pass (fresh --clear-cache\nrun).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T07:54:58+10:00",
          "tree_id": "4f26f1d709c8d8092341611d64b998e587a49981",
          "url": "https://github.com/deconstructo/curry/commit/12e37673b3918d19de25ce0d0986834cd9f9b1dc"
        },
        "date": 1788213355889,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 13.011,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 26.058,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.269,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 31.413,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 88.143,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 220.181,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 53.936,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 61.867,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 46.501,
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
          "id": "54586312731e1e4009cc2c6126258dffc7af1319",
          "message": "test(ci): bound websocket/websocket_server/ros to a 60s timeout\n\nCloses #110 (mitigation only, root cause still open).\n\nFound investigating a ~28-minute CI job cancellation on an unrelated\nPR: the 'websocket' ctest target hung completely silent (no output at\nall) until GitHub killed the job, reproduced identically on a manual\nre-run. In the same run, 'ros' failed fast with a port-bind conflict\n-- these three tests all bind fixed port numbers, making them\ninherently vulnerable to contention under ctest -j parallel CI load.\n\nSame treatment this file already gives the 'actors' test's own\ndocumented rare-hang: a TIMEOUT turns a future recurrence into a\nfast, diagnosable CTest TIMEOUT instead of silently consuming an\nentire CI job. Does not fix the underlying cause (why the blocking\ncall has no timeout of its own, or the fixed-port design) -- see the\nlinked issue.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T09:31:09+10:00",
          "tree_id": "3fa6472cd301058318b07739666eb03a0a71b1a2",
          "url": "https://github.com/deconstructo/curry/commit/54586312731e1e4009cc2c6126258dffc7af1319"
        },
        "date": 1788219112695,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.292,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.062,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.78,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 39.207,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 131.079,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 287.825,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.914,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.72,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.216,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "3ed54eaa908d7e1992d059aa184436074a92c070",
          "message": "fix(reader): reject a bare dot at the start of a list instead of misreading it (#109)\n\nCloses #103.\n\n\"(. r)\" (a dot with nothing preceding it) previously read as a valid\n2-element list containing the literal symbol \".\" followed by \"r\",\ninstead of being rejected. Per R7RS's own grammar (\"(datum*)\" or\n\"(datum+ . datum)\"), a dot needs at least one datum before it to be\na legal dotted-pair tail -- \"(. r)\" matches neither production.\n\nRoot cause: read_list's dot-awareness lived entirely in\nread_list_tail, consulted for every list element AFTER the first.\nread_list itself read its own head element via a plain read_datum\ncall with no dot-checking at all, so a list literally starting with\n\".\" fell through to being read as an ordinary one-character symbol\ntoken.\n\nFixed by giving read_list the same dot-followed-by-a-delimiter check\nread_list_tail already has for its head element, raising a clean\nread-error instead. A dot that turns out to start a LONGER token\n(\"...\", \".foo\") is still read correctly as that symbol, via the\nsame manual sb_push token-building technique read_list_tail already\nuses for its own identical case -- discovered along the way that\nport_unread_char (the more obvious fix -- consume the dot, peek\nahead, put it back if not a bare dot) is declared in port.h but was\nnever actually implemented anywhere in the codebase; not implementing\nit now, since the manual-token-building approach avoids needing it\nat all and mirrors existing code exactly.\n\n9 new regression tests (reader_dotted_list_tests.scm): the exact\nrejection case (bare and nested), every legitimate dotted-pair shape\nunaffected, '...' and other dot-prefixed symbols as a list's head\nelement specifically (the case the fix's own token-building branch\nhas to get right, not just the rejection case), and (scheme\ncase-lambda)'s legal bare-symbol variadic clause shape confirmed\nunaffected.\n\n115/115 ctest suites pass (fresh --clear-cache run).\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T09:38:07+10:00",
          "tree_id": "80ec0dff3ac1d7fc972acdc9a224659c90940a21",
          "url": "https://github.com/deconstructo/curry/commit/3ed54eaa908d7e1992d059aa184436074a92c070"
        },
        "date": 1788219525545,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.321,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.156,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.651,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.239,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 129.991,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 283.574,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.583,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.169,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 67.72,
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
          "id": "532d8d46b2c289ac0856c9f81f8ef87aa4dca315",
          "message": "Release v1.23.5\n\nDocumentation sweep (CLAUDE.md's GLOBAL_ENV-aliasing note, case-lambda.sld's\nnow-stale apply-TCO limitation comment) plus a CHANGELOG.md entry covering\neverything landed since v1.23.4: case-lambda, OP_TAIL_APPLY, the\n_Thread_local C++ portability fix, the LLVM hasTerminator() fix, the\nsyntax-rules nested-ellipsis fix, the scc.c upval_count validation, the\nreader's bare-dot-at-list-start fix, and the websocket/ros CI timeout.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T09:46:54+10:00",
          "tree_id": "69156fbbb804af0bd1f5c66af6bbf1d6c20570c8",
          "url": "https://github.com/deconstructo/curry/commit/532d8d46b2c289ac0856c9f81f8ef87aa4dca315"
        },
        "date": 1788220055313,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.018,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 26.423,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.181,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.269,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 133.225,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 285.646,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.787,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.775,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.334,
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
          "id": "ef4dbedeacd826228e8e6532a6a694d6e3afa3c1",
          "message": "Update Formula/curry.rb sha256 for v1.23.5",
          "timestamp": "2026-09-01T09:47:31+10:00",
          "tree_id": "d55022060b213a80878ee1a55db7539e2ec9dd5c",
          "url": "https://github.com/deconstructo/curry/commit/ef4dbedeacd826228e8e6532a6a694d6e3afa3c1"
        },
        "date": 1788220098434,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 10.115,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 14.904,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 2.767,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 18.163,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 65.986,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 155.095,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 42.601,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 47.658,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 35.039,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "275c01540fb64fc74e50c9f28f67d9d00548157f",
          "message": "Merge pull request #112 from deconstructo/fix/llvm-hasterminator-portability\n\nfix(llvm): drop hasTerminator() dependency, breaks build on LLVM 15",
          "timestamp": "2026-09-01T10:12:46+10:00",
          "tree_id": "75f7f61afede39f268d03cf61574a2666d9fca90",
          "url": "https://github.com/deconstructo/curry/commit/275c01540fb64fc74e50c9f28f67d9d00548157f"
        },
        "date": 1788221600198,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.372,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 23.523,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.798,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.173,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 101.787,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 243.17,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 61.963,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 69.782,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 53.89,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "42d0d67223d14c06c4ccc882088536c0541208eb",
          "message": "Merge pull request #116 from deconstructo/fix/jit-macro-bailout\n\nfix(llvm): decline JIT promotion for macro calls and unimplemented special forms",
          "timestamp": "2026-09-01T11:23:46+10:00",
          "tree_id": "77e5ea309e34becfe372780cf6cdbbac11939129",
          "url": "https://github.com/deconstructo/curry/commit/42d0d67223d14c06c4ccc882088536c0541208eb"
        },
        "date": 1788225863448,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.618,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 23.536,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.843,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 33.023,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 102.533,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 248.32,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.621,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 72.029,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 56.087,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "9119c0fe480022ecb3c2f6668a1027314f727008",
          "message": "Merge pull request #113 from deconstructo/ci/add-llvm-job\n\nci: add a BUILD_LLVM=ON job so the JIT backend gets any CI coverage at all",
          "timestamp": "2026-09-01T11:30:01+10:00",
          "tree_id": "4928284db6f0efca2c1a1a58ba257494a5eeb713",
          "url": "https://github.com/deconstructo/curry/commit/9119c0fe480022ecb3c2f6668a1027314f727008"
        },
        "date": 1788226246708,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.962,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.073,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.184,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.822,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 133.191,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 288.03,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.795,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 92.01,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 70.961,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "05c33df8ffb926982408a56692cc0c7f8394afcf",
          "message": "Merge pull request #121 from deconstructo/fix/jit-scoping-gaps\n\nfix(llvm): close three local-shadowing/scoping gaps in the JIT tier",
          "timestamp": "2026-09-01T13:10:13+10:00",
          "tree_id": "285274c34fc015299422ba52a935bdb889e87f12",
          "url": "https://github.com/deconstructo/curry/commit/05c33df8ffb926982408a56692cc0c7f8394afcf"
        },
        "date": 1788232252662,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.125,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.998,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.632,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.78,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 132.506,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 292.718,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 70.746,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.522,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.405,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "6dbe069e8f29d3433c1be47e42afbedafd268184",
          "message": "Merge pull request #122 from deconstructo/fix/jit-arith-global-deopt\n\nfix(llvm): deoptimize JIT-compiled closures on global arithmetic redefinition",
          "timestamp": "2026-09-01T15:15:03+10:00",
          "tree_id": "37d5254a3262545103a0a00393e49c0477acc5bc",
          "url": "https://github.com/deconstructo/curry/commit/6dbe069e8f29d3433c1be47e42afbedafd268184"
        },
        "date": 1788239742385,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.283,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.107,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.706,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.117,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 128.56,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 287.915,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.748,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 91.416,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.079,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "5def5a7e341bfe84586235630e0e9fcab1fc4ef6",
          "message": "Merge pull request #126 from deconstructo/fix/named-let-init-scoping\n\nfix(compiler): named let's own name is not visible during its own init expressions",
          "timestamp": "2026-09-01T16:00:13+10:00",
          "tree_id": "76d0f5cade81d07bf8a88dad54c4b09d9a76e065",
          "url": "https://github.com/deconstructo/curry/commit/5def5a7e341bfe84586235630e0e9fcab1fc4ef6"
        },
        "date": 1788242457792,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.374,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 23.641,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.823,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 29.076,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 114.701,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 260.722,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 64.017,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 74.689,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 53.972,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "1c86d536404efc4db5878a5bce1f73580ad3b892",
          "message": "fix(compiler): malformed let/letrec/do/guard bindings and unbounded let* recursion (#124, #125) (#130)\n\n* fix(compiler): catch malformed let/letrec/do/guard bindings and unbounded let* recursion\n\nIssue #124: `(let ((a)) ...)` and similar malformed bindings for\nlet/let*/letrec/letrec*/do/let-syntax/letrec-syntax/guard crashed the\nprocess (SIGSEGV) instead of raising a catchable compile-time error,\nacross all three of curry's independent compilation paths for these\nforms: the Tier 2.1 IR lowerer (ir_lower.c), the classic bytecode\ncompiler (compiler_classic.c -- the only live path for do/let-syntax/\nguard, which have no IR lowering at all), and the tree-walking\nevaluator (eval.c). Fixed by validating each binding's shape (a pair\nwhose cdr is also a pair, i.e. `(name init)`) before destructuring it,\nusing the existing require_min_args idiom in the two compilers and a\nsmall local equivalent in eval.c. Also fixed two related crashes not\nin the original report: let-syntax with a bodyless binding, and guard\nwith an empty clause.\n\nIssue #125: a long flat let*/letrec*/do chain (a few hundred sequential\nbindings) SIGSEGVed the process during compilation, because ir_emit()\nrecurses once per binding with no tail-call reuse of its own C frame,\neventually exhausting the real C stack with no bound. eval() already\nhad its own guard against exactly this kind of unbounded C recursion;\nextracted it into a shared check_c_stack_depth() (runtime.c) and now\ncall it from both eval() and ir_emit()'s single entry point, so the\nsame chain now raises a clean, catchable stack-overflow condition\ninstead of crashing.\n\nRegression tests added to tests/r7rs_tests.scm (tree-walker path, via\neval) and tests/test_cli.sh (real compiler path, via subprocess --\neval() can't exercise ir_lower.c/compiler_classic.c/ir_emit.c at all).\nThe test_cli.sh stack-overflow test needed 3001 bindings rather than\n~220: the guard fires at a fraction of the real per-thread stack limit,\nwhich is queried from the live ulimit rather than fixed, and CTest's\nown test-runner launches this script under a much larger default stack\n(64MB) than an interactive shell (8MB) -- confirmed empirically, fixed\nso the test is robust regardless of the caller's ulimit.\n\nVerified every fix both by confirming the crash becomes a clean exit-1\nerror and by confirming ordinary, legitimate usage of the same forms\nis unaffected. Verified the #125 test actually catches a regression by\ntemporarily reverting the check_c_stack_depth call and confirming the\ntest fails with a SIGSEGV (exit 139) as expected, then restoring it.\n\n117/117 ctest suites pass (default build), 118/118 pass (LLVM build),\nboth with .scc caches cleared. The one intermittent failure seen during\na parallel run (`ros`, port-bind contention) is a known pre-existing\nflake unrelated to this change, confirmed by rerunning in isolation.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(compiler): close eval.c gaps and stack-guard bypass found by review\n\nIndependent code review and security review of the prior commit\n(fix/#124-#125) surfaced real defects in scope of those same two\nissues, verified and fixed here:\n\n- eval.c had several more unchecked-destructure crashes in the exact\n  form family #124 targeted, not caught by require_binding_shape alone\n  since that only validates an individual binding's shape, not that\n  the enclosing form has enough top-level pieces to destructure:\n  `(let* ((a 1)))` / `(letrec ((a 1)))` with no body crashed on an\n  unconditional vcdr of a nil body; `(let loop)` with no bindings/body\n  crashed on vcar of a nil body; `(do ((i 0 (+ i 1))))` with no test\n  clause and `(do ((i 0)) ())` with a non-pair test clause both\n  crashed; and a dotted step-spec like `(i 0 . 5)` dereferenced the\n  non-pair tail via vcaddr. All now raise (or, for the dotted-step\n  case, silently skip the step -- matching compiler_classic.c's own\n  existing convention for the identical shape) instead of SIGSEGVing.\n- ir_lower_letrec (shared by S_LETREC/S_LETREC_STAR) hardcoded\n  \"letrec\" in its raised error message even for a letrec*-specific\n  malformed binding -- cosmetic, but a wrong form name in an error\n  points a future debugger at the wrong code. Now takes the actual\n  head symbol and names itself correctly.\n- The #125 stack-depth guard's per-thread stack-size query was\n  unclamped: under `ulimit -s unlimited` (a real, common setting, e.g.\n  on Linux service defaults), pthread_getattr_np/pthread_attr_getstack\n  reports a synthetic ~93TB stack size, pushing the guard's threshold\n  out past any depth actually reachable before a real SIGSEGV --\n  disabling the guard exactly where an unbounded stack makes a runaway\n  recursion most dangerous to run unguarded. Now clamped to a 512MB\n  ceiling, comfortably above every real stack size curry itself\n  requests.\n- GC_get_stack_base's return value was unchecked; on GC_UNIMPLEMENTED\n  the stack base is indeterminate, corrupting the guard's arithmetic\n  into either a persistent per-thread spurious stack-overflow (bad\n  base cached forever) or a silently disabled guard. Falls back to the\n  address of a local variable in this stack frame on failure -- an\n  underestimate of the true base, so the guard now errs toward firing\n  slightly early rather than not at all.\n\nAlso verified, not changed: the reviews confirmed guard's weaker\nsingle-element-clause check is correct R7RS behavior, the ir_lower.c\nclosedness predicates' fail-safe `return false` (rather than raising)\non a malformed binding is the right choice since the real lowering\nfunctions separately validate and raise, and check_c_stack_depth's\nplacement at ir_emit's single entry point does cover the whole\nrecursive tree (both ir_emit_inline_call call sites are internal to\nir_emit).\n\nFiled as separate issues, out of #124/#125's own scope (different\nspecial forms / different subsystem): unchecked cond/case clause\ndestructuring in compiler_classic.c and eval.c (same crash class, but\na different form family); the equivalent eval.c gaps for let-values/\nlet*-values/parameterize/when/define-syntax; and the reader having no\nrecursion-depth or list-length limit at all (a flat ~50000-element\nlist or ~15000-deep nesting SIGSEGVs at read time, before any of this\ncommit's guards ever run).\n\nRegression tests added to both tests/r7rs_tests.scm (the new eval.c-\nonly gaps, via eval) and tests/test_cli.sh (the letrec*/letrec naming\nfix, which is a compile-time error for the whole enclosing top-level\nform and so can't be tested via a runtime guard from inside a script).\n\n362/362 (r7rs_tests.scm), 87/87 (test_cli.sh), 117/117 ctest (default\nbuild), 118/118 ctest (LLVM build) -- all with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(tests): raise #125's stack-overflow test threshold for Release builds\n\nCI's macOS Release build failed test_cli.sh's #125 regression test\n(expected exit 1, got 0) at the 3001-binding threshold that was\nsufficient locally: a Release build's optimizer shrinks ir_emit's own\nper-recursion-level stack frame enough that 3001 levels of recursion\nno longer reaches check_c_stack_depth's threshold, on top of the\nalready-known ulimit-driven variance (CTest's 64MB default stack vs.\nan interactive shell's 8MB). Verified empirically that ~8000 bindings\nis enough to trigger reliably under a local Release build; raised to\n20001 for comfortable margin, confirmed under both Debug and Release\nbuilds combined with both an 8MB and a 64MB ulimit, and confirmed to\nstay well under the reader's own unrelated ~50000-element recursion\nlimit (issue #129) so the test keeps testing ir_emit's guard\nspecifically.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(tests): run #125's oversized let* form as a script file, not -e\n\nThe 20001-binding form from the previous commit's threshold fix is\n~300KB of source text -- comfortably past Linux's default whole-argv\nlength limit. CI's ubuntu runners (Debug, Release, and the LLVM build)\nall failed the same way: exit 126, bash's own report for an execve()\nE2BIG (\"argument list too long\") when handing that string to `curry -e`\nas a single argument. Fixed by writing the form to a real .scm file in\nthe suite's existing $TMPDIR_CLI and running it as a positional script\nargument instead, which has no such limit.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T22:50:18+10:00",
          "tree_id": "2410ff5c2f66a374d1a03e1921b89c33a468a98b",
          "url": "https://github.com/deconstructo/curry/commit/1c86d536404efc4db5878a5bce1f73580ad3b892"
        },
        "date": 1788267068690,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.682,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.87,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.897,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 35.557,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 133.71,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 289.281,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.495,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.792,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.683,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "eb099a1015bcdad5b34939eb397fcf3a05579bfd",
          "message": "fix(compiler,reader): unchecked cond/case, eval.c gaps, unbounded reader recursion (#127, #128, #129) (#131)\n\n* fix(compiler,reader): unchecked cond/case, eval.c gaps, unbounded reader recursion\n\nThree follow-up bugs found by independent code/security review of the\n#124/#125 fix (PR #130), filed as #127/#128/#129 and fixed here:\n\nIssue #127: compile_cond/compile_case (compiler_classic.c) and eval.c's\nown S_COND/S_CASE handlers had the identical unchecked-clause-\ndestructure crash the #124 fix closed for the let/letrec/do family, but\nmissed entirely since cond/case is a different form family, not a\nvariant of let. `(cond ())`, `(cond 1)`, `(case 1 ())`, `(case 1 1)`,\nand `(case)` (no key) all SIGSEGVed instead of raising. Fixed with the\nsame require_min_args/pair-check idiom used throughout #124. Also\ncorrected a comment (compiler_classic.c, in compile_guard) that had\nclaimed compile_cond already validated its clauses -- true again now\nthat this fix lands, but was false when first written and is exactly\nwhat led review to file this issue in the first place.\n\nIssue #128: several more eval.c-only unchecked-destructure gaps in the\nsame class, found by independent code review -- let-values/let*-values\n(non-pair or malformed binding, and a missing-body gap matching #124's\nown let*/letrec pattern), parameterize (non-pair binding, and a value\nthat isn't actually a parameter object -- read directly via as_param\nrather than through apply, so unlike the compiled path it needs its\nown explicit type check), when/unless (missing test, and a\nmissing-body gap for a true/false test with no body forms), and\ndefine-syntax (missing name/transformer). All are tree-walker-only:\nthe corresponding compiler paths already validated correctly for the\nsame malformed input.\n\nIssue #129: the reader itself (src/reader.c) had no recursion-depth or\nlist-length limit at all. read_list and read_list_tail are mutually\nrecursive once per list element (both for a flat list's length and for\nnested-list depth); read_datum recurses once per prefix reader macro\n(quote/quasiquote/unquote). A large enough flat list, deep enough\nnesting, or a long enough quote-chain all SIGSEGVed at READ time,\nbefore any of #125's own guards (eval()/ir_emit(), both compile-time)\nare ever reached -- read happens first. Fixed by sharing the same\ncheck_c_stack_depth guard #125 added (src/runtime.c), called from all\nthree of read_datum/read_list/read_list_tail.\n\nRegression tests added to tests/r7rs_tests.scm (the reader is a single\nshared implementation, unlike eval() vs. the compilers, so `read` on a\nstring port genuinely exercises the same code a top-level script parse\nwould) and tests/test_cli.sh (cond/case's compiler-path halves, which\nneed a real subprocess compile for the same reason #124's own do/\nlet-syntax/guard checks do).\n\nThe reader test thresholds needed to go well past the sizes that\ncrashed the pre-fix binary (~15000-60000): the same ulimit-driven\nvariance #125's own test threshold hit (CTest's 64MB default stack vs.\nan interactive shell's 8MB), compounded by a Release build's optimizer\nshrinking the per-recursion-level stack frame -- confirmed empirically\nthat a Release+64MB-stack combination needed >600000 for the tightest\nof the three vectors. Settled on 1000000, confirmed fast (well under a\nsecond) and reliable across Debug/Release x 8MB/64MB stack locally.\n\n383/383 (r7rs_tests.scm), 92/92 (test_cli.sh), 117/117 ctest (default\nbuild, one pre-existing flaky websocket port-bind failure confirmed\nunrelated by rerunning in isolation), 118/118 ctest (LLVM build) --\nall with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(eval): close remaining cond/case/let-values/parameterize crash gaps\n\nSecond round of independent code/security review of the #127/#128 fix\n(commit 2daa066) found the fix was incomplete -- the same bug class\nsurvived in adjacent branches of the code it edited:\n\n- cond: `(cond (1 . 2))` and `(cond (else . 2))` -- an improper\n  (non-nil, non-pair) clause tail -- still reached vcar(body) on a\n  non-pair; `(cond (1 =>))` -- an arrow clause with no receiver\n  expression -- still reached vcadr(body) past the end. Both now\n  validated before use, matching compile_cond's own equivalent checks.\n- case: the same two gaps (`(case 1 ((1) . 2))`, `(case 1 ((1) =>))`),\n  plus case's own `else` clause had a SEPARATE, incomplete copy of the\n  matched-datum branch's logic -- `(case 1 (else))` (no body at all)\n  crashed because else's copy was missing the `if (vis_nil(body))\n  return V_VOID;` its sibling branch already had, and `(case 1 (else\n  =>))` had the same missing-receiver gap as the matched-datum branch.\n- let-values/let*-values: `(let-values)` / `(let*-values)` -- no\n  bindings list at all -- still reached vcar(rest) unguarded; the\n  per-binding check added in the first #128 fix only fires once a\n  bindings list is already being iterated, not when there's no list.\n- parameterize: same missing `rest`-level check for `(parameterize)`.\n  Also found (not previously flagged, same root cause): `(parameterize\n  ((p v)))` with no body at all crashed at vcar(body) on a nil body.\n  Fixed to match compile_parameterize's own leniency here rather than\n  introduce a stricter tree-walker-only rejection: a zero-body\n  parameterize desugars (compiled path) to a zero-body lambda, which\n  compile_seq already treats as a no-op returning void, not an error --\n  confirmed empirically that `(parameterize ())` and `(parameterize\n  ((p 1)))` both compile and run to void today, so eval.c now matches\n  that instead of raising.\n\n11 new regression tests added to tests/r7rs_tests.scm covering every\none of the above, plus confirming the two intentionally-lenient empty-\nbody cases (`(case 1 (else))`, `(parameterize ())`) return void rather\nthan raising, matching the compiled path exactly.\n\n394/394 (r7rs_tests.scm), 92/92 (test_cli.sh), 117/117 ctest (default\nbuild, no flakes this run), 118/118 ctest (LLVM build) -- all with\n.scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-01T23:32:18+10:00",
          "tree_id": "ae6f4cea0e5616ad4d3db55c077753fd3bd2f5f3",
          "url": "https://github.com/deconstructo/curry/commit/eb099a1015bcdad5b34939eb397fcf3a05579bfd"
        },
        "date": 1788269579491,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.781,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.975,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.633,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.642,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 136.51,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 280.589,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.13,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 90.685,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.614,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "03a30f9aba54b8fbfb5ae3e75ba39dcbdb5e1e78",
          "message": "fix(eval,port): systemic eval.c argument-shape gaps and printer recursion (#132, #133) (#136)\n\n* fix(eval,port): systemic eval.c arg-shape gaps and printer recursion\n\nFixes #132 and #133, both filed from independent review of the #127/\n#128 fix (PR #131) but scoped separately since they're distinct\nsubsystems.\n\nIssue #132: eval.c's special-form handlers had the same unchecked-\n`rest`-destructure crash class as #124/#127/#128 across a much wider\nset of forms -- `(if)`, `(if test)`, `(lambda)`, `(define)`, `(set!)`,\n`(set! x)`, `(define-values)`, `(define-values (a))`, `(quasiquote)`,\n`(call-with-values)`, `(call-with-values p)`, `(call/cc)`, `(guard)`,\n`(guard ())`, and an import `#:keyword` modifier with no argument\nafter it, all SIGSEGVed. Added a general require_min_args helper to\neval.c (mirroring compiler.c's own, which eval.c has no access to --\nsame reasoning as the existing require_binding_shape, now reimplemented\nas a thin wrapper over the new helper) and applied it at each of these\nsites. guard's own missing-body case is intentionally left lenient\n(returns void, matching compile_guard's confirmed leniency for the\nsame input) rather than raising, mirroring the empty-body leniency\nalready established for let/let*/letrec/let-values/parameterize.\n\nIssue #133: the printer (write/display, src/port.c) had the identical\nunbounded-recursion class #129 fixed for the reader, but for runtime-\nconstructed data rather than source text -- ws_count_refs/ws_write\n(the write/display-shared machinery) and plain scm_write (write-simple,\nand the debugger's own value printing) all recurse once per level of\nCAR nesting with no bound, since only the reader's own guard (#129)\ncovers literal source text, not data built at runtime via repeated\n`cons`/`list`. Fixed by sharing check_c_stack_depth (the same guard\n#125/#129 already share) at all three functions' own single entry\npoints.\n\nBoth reader/printer thresholds needed to go well past a first-pass\nguess: same ulimit- and Release-build-optimizer-driven variance #125/\n#129's own test thresholds hit, confirmed empirically per vector.\n\nRegression tests added to tests/r7rs_tests.scm: 20 new checks for\n#132 (via eval, since these are compile-time-safe forms whose\ncompiled-path equivalents were already confirmed correct) and 4 new\nchecks for #133 (write/write-simple/display on a deeply car-nested\nruntime-built list, plus an ordinary-depth sanity check).\n\n415/415 (r7rs_tests.scm, confirmed across Debug/Release builds x 8MB/\n64MB stack ulimits), 92/92 (test_cli.sh), 117/117 ctest (default\nbuild, one pre-existing flaky websocket port-bind failure confirmed\nunrelated by rerunning in isolation), 118/118 ctest (LLVM build) --\nall with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(eval,runtime): close remaining #132/#133 gaps found by review\n\nA second round of independent code/security review of the #132/#133\nfix found the same bug classes still reachable in adjacent code:\n\neval.c (issue #132, continued):\n- `(let)`, `(let*)`, `(letrec)`, `(letrec*)`, `(let-syntax)`,\n  `(letrec-syntax)` -- missing bindings/body entirely -- still crashed\n  on their own top-level vcar(rest); the first pass only checked forms\n  with a small fixed argument count, not this \"at least the bindings\n  position must exist\" shape shared by the whole let family.\n- A new, wider class the first pass didn't consider at all: an\n  IMPROPER (non-nil, non-pair) body -- `(let () . 5)`, `(when #t . 5)`,\n  `((lambda () . 5))`, `(delay . 5)`, `(guard (e) . 5)`, `(parameterize\n  () . 5)`, `(do () (#t . 2))`, `(begin . 5)`, and named let's own\n  variant -- reaches vcdr(body) on a non-pair in the body-sequencing\n  loop nearly every one of these forms shares, including the shared\n  closure-application trampoline every ordinary function call goes\n  through (so a malformed lambda/define/named-let body crashed on\n  first CALL, not at creation time). Added a require_body_shape helper\n  (checks nil-or-pair, raising only for the genuinely malformed case --\n  callers keep their own existing nil-means-void leniency) and applied\n  it at every site above, plus at closure-creation time (lambda,\n  define's lambda-sugar, named let, delay/delay-force) rather than at\n  every call site, since all of those funnel through one shared\n  application path.\n- `guard`'s two clause-body branches (`else`, and the matched-test\n  branch) had their own separate, incomplete copies of this same\n  check -- `(guard (e (else)) ...)` and `(guard (e (else . 2)) ...)`\n  crashed since else's copy never got the nil/shape guard cond's own\n  else branch has had since #127; the matched-test branch's improper-\n  tail case now falls back to returning the test's own value, matching\n  the compiled path's own confirmed (if slightly unusual) leniency for\n  that exact shape.\n- `(set! 1 2)` was a type-confusion bug, not just a missing-check\n  crash: sym_cstr(sym) unconditionally read a non-Symbol value as if\n  it had a Symbol's own header/data layout -- a fixnum crashed\n  outright, and other heap types read memory at the wrong struct-field\n  offset and formatted whatever was there into the raised error\n  message (an out-of-bounds heap read reachable from untrusted\n  source). Fixed with an explicit vis_symbol check before use.\n\nruntime.c (issue #133, continued):\n- Raising an UNCAUGHT exception whose own payload is deeply CAR-nested\n  turned #133's stack-depth guard into an unbounded print-raise cycle\n  instead of a clean abort: the guard fires (correctly) while\n  scm_write_shared is trying to print the exception in scm_raise_val's\n  \"unhandled\" path, which raises a new stack-overflow condition, which\n  -- with no handler installed -- lands right back at that same\n  \"unhandled\" path, which tries to print AGAIN while still at the same\n  stack depth, firing the guard again immediately. Observed thousands\n  of repeated \"Unhandled exception:\" lines before eventually SIGSEGVing\n  once even the guard's own emergency margin was exhausted. Fixed with\n  a thread-local re-entrancy flag: a second entry into the unhandled-\n  exception path skips the recursive printer call entirely and aborts\n  with a plain, non-recursive fallback message instead.\n\nAlso filed #134 (out of scope here, separate subsystem): independent\nreview found the symbolic CAS module has a related but distinct\nunbounded-recursion problem, both during expression-tree construction\n(src/symbolic.c) and in its own dedicated printers (sx_write/sp_infix/\nsl_latex, src/symbolic_print.c), neither of which #133's printer guard\ncovers.\n\n27 new regression tests added to tests/r7rs_tests.scm.\n\n442/442 (r7rs_tests.scm, confirmed across Debug/Release builds x 8MB/\n64MB stack ulimits), 92/92 (test_cli.sh), 117/117 ctest (default\nbuild, one pre-existing flaky websocket port-bind failure confirmed\nunrelated by rerunning in isolation), 118/118 ctest (LLVM build) --\nall with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(eval,runtime): close define/with-assumptions gaps from third review\n\nA third independent review round found two more sites in the same\n\"improper body\" crash class the previous commit's require_body_shape\nsweep didn't reach:\n\n- `(define x . 5)` -- S_DEFINE's symbol-name branch had its own\n  separate ternary (`vis_nil(vcdr(rest)) ? V_VOID : eval(vcadr(rest),\n  env)`) for the missing-value case; the require_body_shape sweep only\n  reached the lambda-sugar branch just below it, not this one. An\n  improper (non-nil, non-pair) tail still reached vcadr(rest) -> a\n  non-pair vcar and crashed.\n- `(with-assumptions () . 5)` -- delegates to eval_body (runtime.c),\n  the shared closure-application trampoline several forms use, which\n  had never been guarded against an improper body at all (only a nil\n  one). Fixed once in the shared function rather than requiring every\n  future caller to remember its own copy of the check.\n\nAlso filed #135 (out of scope here): the same review round found\ndefine-record-type and syntax-rules crash on malformed input on BOTH\nthe compiled and tree-walked paths -- a different class from every\nprior issue in this series, since those were all tree-walker-only gaps\nwhere the compiler already validated correctly.\n\n2 new regression tests added to tests/r7rs_tests.scm.\n\n444/444 (r7rs_tests.scm), 92/92 (test_cli.sh), 117/117 ctest (default\nbuild, no flakes this run), 118/118 ctest (LLVM build) -- all with\n.scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-02T18:40:53+10:00",
          "tree_id": "e25dd3967d7f907a45d83ee8b4c06caf3a0b0c76",
          "url": "https://github.com/deconstructo/curry/commit/03a30f9aba54b8fbfb5ae3e75ba39dcbdb5e1e78"
        },
        "date": 1788338494668,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 16.983,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.083,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.611,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 35.8,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 133.766,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 284.255,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.24,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.926,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.283,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "9d4d57b1ce677de92437a7dfa6655d578755b15c",
          "message": "fix(symbolic,numeric,set): unbounded recursion in CAS, tuple arithmetic, and equal?/hash (#134) (#138)\n\n* fix(symbolic): share stack-depth guard across CAS construction/printing\n\nFixes #134: the symbolic CAS subsystem (src/symbolic.c,\nsrc/symbolic_print.c) has the same unbounded-recursion class #125/\n#129/#133 already fixed for eval()/ir_emit(), the reader, and the\nprinter, but for symbolic expression trees specifically -- a deeply\nnested expression (e.g. repeated `(sin ...)` wrapping, or any script\nthat recursively wraps a sym-var) SIGSEGVs during construction/\nsimplification, before #133's printer guard is ever reached (that\nguard only helps once a value already exists and is being written).\n\nEvery self-recursive expression-tree walker in symbolic.c now shares\ncheck_c_stack_depth (src/runtime.c): sx_simplify, sx_diff, sx_wirtinger,\nsx_substitute, sx_expand, sx_integrate, sx_limit_inner (the tree-\nstructural recursion there, distinct from its own existing\nLHOPITAL_MAX-bounded iteration-count recursion), sx_fracdiff,\nsx_fracint, sx_laplace, sx_ilaplace, sx_fourier, and sx_ifourier.\nsx_collect calls sx_expand immediately and has no separate self-\nrecursion of its own, so it's covered transitively.\n\nsymbolic_print.c's three dedicated writers -- sx_write (prefix,\nsym->string), sp_infix (sym->infix), and sl_latex (sym->latex) -- self-\nrecurse into their own expression-tree traversal without ever going\nthrough scm_write except for a numeric leaf, so #133's printer guard\ndidn't cover their own tree-walk above that. All three now share the\nsame guard.\n\nA regression test using the straightforward \"wrap N times, one sin\napplication per iteration\" construction turned out to cost O(depth-at-\nguard^2), not O(N): sx_simplify re-walks its entire argument tree from\nscratch on every call (no memoization of already-simplified\nsubexpressions), and how deep the chain gets before the guard fires\nscales with the real available stack -- confirmed to take 30+ seconds\nunder a Release build with a real 64MB stack, the same threshold-\ninflation #125/#129/#133's own tests hit for a different reason.\nRather than chase an ever-larger N, the test in tests/test_cli.sh\nexplicitly constrains the child process's own stack via `ulimit -s`\nfirst, making the guard's threshold small, deterministic, and fast\n(well under a second) regardless of host environment or build type.\n\n406/406 (numeric_ext_tests.scm, the primary symbolic-CAS test file,\nunaffected), 94/94 (test_cli.sh), 117/117 ctest (default build),\n118/118 ctest (LLVM build) -- all with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(symbolic,numeric,set): close remaining #134 gaps found by review\n\nIndependent code/security review of the first #134 fix found more\nself-recursive tree-walkers in the same class, plus two real crashes\nstill reachable after the initial guards landed:\n\nsymbolic.c: added the same check_c_stack_depth guard to six more\nself-recursive functions the first pass missed: sx_equal (structural\nequality), sx_trigsimp, sx_is_nc (non-commutativity test), sx_degree_long\n(polynomial degree), and the mutually-recursive sx_mul_for_ratio/\nsx_ratio_simplify pair. Most importantly, sx_depends_on: independent\nsecurity review found this one still crashed at unbounded depth\ndespite sx_integrate/sx_limit/sx_series/sx_laplace/sx_ilaplace/\nsx_fourier/sx_ifourier's own guards already having run, because it's\ncalled as their first structural check on a `up`/`down` tuple\nexpression -- tuples build nesting in O(1) per level with no\nsimplification pass, so unlike ordinary symbolic-expression\nconstruction (capped by sx_simplify's own guard), a tuple's depth is\nnever bounded before it reaches here.\n\nnumeric.c: num_mul has its own separate inline tuple-distribution loop\n-- unlike num_add/num_sub/num_neg, which all route through the\nalready-guarded tuple_binop/tuple_unop -- that recursed into a nested\ntuple with no bound of its own. This was the actual remaining crash\nfor `(∫ big x)` on a deeply up-wrapped tuple even after sx_depends_on\nwas fixed.\n\nset.c: scm_equal/val_hash have the identical unbounded-recursion\nclass, but reachable from ordinary Scheme data with no symbolic module\ninvolved at all -- a deeply nested list compared with `equal?` (or\nused as a hash-table key) SIGSEGVed. Broader-impact than the rest of\n#134 since set.c backs `equal?` and hash tables generally; fixed here\nrather than filed separately since it's the same trivial guard.\n\nsymbolic_print.c: also guarded sxc_write (the 'cuneiform notation\nwriter), a fourth self-recursive symbolic printer the first pass\nmissed -- not independently demonstrated to be exploitable (its own\nper-level stack frame is far smaller than the construction-side\nfunctions, so a tree only reaches it if already shallow enough to have\nsurvived construction), added for defense-in-depth/consistency with\nthe other three writers.\n\nFiled #137 (out of scope here): independent review also found\nsx_simplify's total lack of memoization is a genuine CPU-exhaustion\nDoS distinct from the stack-depth issue this fix closes -- an\nO(depth^2) re-walk that can burn tens of seconds of CPU under a\ngenerous stack ulimit before the guard ever fires. Fixing that\nrequires real memoization, a bigger change than a stack guard.\n\n10 new regression tests added to tests/r7rs_tests.scm (the up-tuple\nand equal? cases build in O(depth), not O(depth^2), so unlike\ntest_cli.sh's own symbolic-construction test they stay fast without\nneeding to constrain the process's own stack). Also improved the\nexisting test_cli.sh symbolic regression test to match on the specific\n\"call stack overflow\" message via error-message rather than accepting\nany raised condition.\n\n448/448 (r7rs_tests.scm), 94/94 (test_cli.sh), 117/117 ctest (default\nbuild), 118/118 ctest (LLVM build) -- all with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-02T20:08:20+10:00",
          "tree_id": "568d2b3b803b5cd02c3f0d24f4bb41f16bef9286",
          "url": "https://github.com/deconstructo/curry/commit/9d4d57b1ce677de92437a7dfa6655d578755b15c"
        },
        "date": 1788343752267,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.604,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.342,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.046,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.45,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 106.453,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 222.895,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 52.001,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 72.271,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 57.7,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "09337ea0ec83427bbd25ff98c13d7fbcd49bfe2d",
          "message": "fix(record-type,syntax-rules): crashes on malformed input on both compiled and tree-walked paths (#135) (#139)\n\n* fix(record-type,syntax-rules): validate malformed input on both paths\n\nFixes #135: unlike every other issue in this series (#124-#132), where\nonly the tree-walker (eval.c) was missing a check the compiler already\nhad, define-record-type and syntax-rules crashed on malformed input on\nBOTH the compiled and tree-walked paths -- because record_type_build_spec\n(src/record_type.c) and sr_compile_fn (src/syntax_rules.c) are each a\nsingle function shared by compiler.c's native codegen and eval.c's own\ntree-walker case, and neither validated its own operand shape.\n\nrecord_type.c (record_type_build_spec, R7RS branch -- the R6RS branch\nwas already defensive):\n- `(define-record-type x)` -- no ctor-form/predicate at all -- and\n  `(define-record-type (x))` -- name itself a list, so vcdr(rest) is\n  nil -- both fell straight into vcadr(rest)/vcaddr(rest) and SIGSEGVed.\n- `(define-record-type point x point? (x px))` -- a non-pair ctor-form\n  -- reached vcar(ctor_form)/vcdr(ctor_form) on a non-pair.\n- `(define-record-type point (mk-point x) point? y)` -- a bare\n  field-spec, not `(field-name getter [setter])` -- reached\n  vcar(vcar(fs)) (the nfields-counting loop) and vcadr(fspec) (the\n  binding-building loop) on a non-pair; both loops re-derive fspec\n  from the same field_specs list, so one validation pass up front\n  covers both.\n\nsyntax_rules.c (sr_compile_fn, called for every `(syntax-rules ...)`\ntransformer-expr):\n- `(syntax-rules x)` -- an ellipsis identifier with nothing after it\n  -- reached vcaddr(form) past the end.\n- `(syntax-rules () ())` -- an empty rule, not `(pattern template)` --\n  reached vcar(rule)/vcadr(rule) on a non-pair. sr_transformer_fn's own\n  identical-looking vcar(rule)/vcdr(rule) needed no separate fix: it\n  only ever walks sr->rules, which sr_compile_fn always builds as\n  genuine (pattern . template) cons pairs via scm_cons, so a malformed\n  RAW rule can never reach it once sr_compile_fn's own input is\n  validated. sr_rebuild_syntax_env (the other constructor for this same\n  shape) was already defensive.\n\nRegression tests added to both tests/r7rs_tests.scm (via `eval` --\ngenuinely exercises the same shared function the compiler also calls,\nunlike #124-#132's own eval-only tests) and tests/test_cli.sh (a real\nsubprocess compile, confirming the top-level compiled path specifically).\n\n455/455 (r7rs_tests.scm), 101/101 (test_cli.sh), 117/117 ctest (default\nbuild), 118/118 ctest (LLVM build) -- all with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(record-type,syntax-rules): close remaining #135 gaps found by review\n\nTwo independent review rounds found the first #135 fix was incomplete\nin both files:\n\nrecord_type.c: the commit's own claim that the R6RS branch was\n\"already defensive\" was wrong -- it had no validation at all.\n- `(define-record-type x (fields (mutable)))` -- a field-spec pair too\n  short to contain a name -- reached vcadr(fspec) on a nil cdr (three\n  separate loops all re-derive fspec from the same field_list, so one\n  validation pass up front covers all three).\n- `(define-record-type x (fields (mutable 42)))` and `(define-record-\n  type x (fields 5))` -- a field-spec whose name isn't a symbol --\n  reached sym_cstr on a non-Symbol value. This is a real out-of-bounds\n  heap read, not just a missing-check crash: sym_cstr blindly reads a\n  Symbol's own header/data-pointer layout off whatever object is\n  actually there, and the resulting garbage bytes get snprintf'd into\n  a generated binding name that user code can then observe.\n- `(define-record-type 42 (fields a))` / `(define-record-type (x)\n  (fields a))` -- a non-symbol record name -- reached the same\n  sym_cstr(name_sym) call, shared by both the R6RS and R7RS branches,\n  so one check covers both.\n\nsyntax_rules.c: the first fix validated that a RULE has the right\nshape (pattern, template), but never that the pattern itself is a\npair/vector rather than a bare atom. `(syntax-rules () (x 1))` and\n`(syntax-rules () (5 1))` passed that check cleanly at DEFINITION time,\nthen crashed sr_transformer_fn's own vcdr(pat) the first time the\nmacro was actually USED -- meaning a malformed macro could be defined\nor loaded successfully and only detonate for whoever later called it.\nFixed by validating the pattern shape in both construction paths for\nthis data structure: sr_compile_fn (the ordinary `(syntax-rules ...)`\npath) and sr_rebuild_syntax_env (the internal %rebuild-syntax-rules\npath used for a compiled top-level macro's runtime re-registration).\n\n15 new regression tests added across tests/r7rs_tests.scm and\ntests/test_cli.sh.\n\n463/463 (r7rs_tests.scm), 108/108 (test_cli.sh), 117/117 ctest (default\nbuild), 118/118 ctest (LLVM build) -- all with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(syntax-rules): close top-level vector-pattern type confusion\n\nA third, final review round found the last hole in #135's own new\npattern-shape check: `vis_pair(pat) || vis_vector(pat)` correctly\nadmits a top-level vector pattern (`#(a b)`), but sr_transformer_fn\nthen called vcdr(pat) directly on the raw Vector object to skip the\nkeyword position -- Pair.cdr and Vector's own first-element slot sit\nat different struct offsets, so this silently reinterpreted adjacent\nmemory as a match-binding value instead of matching (or failing to\nmatch) correctly. Confirmed as real type confusion, not just a\ntheoretical gap: `(define-syntax m (syntax-rules () (#(a) 'a))) (m 1 2\n3)` bound `a` to the whole argument list `(1 2 3)` instead of raising\nor cleanly failing to match. For `#()` specifically (a zero-length\nvector pattern, converting to an empty list), the same code path would\nhave gone on to crash on vcdr(()).\n\nFixed by converting a vector pattern to a list first (matching how\nsr_match_one already handles a NESTED vector sub-pattern, just not\nthis top-level case), and treating a resulting empty list (`#()`, with\nno keyword position at all) as \"this rule can never match\" rather than\nraising -- a macro can have other rules that do match the same use, so\nthis mirrors how an ordinary non-matching rule is already handled.\n\n3 new regression tests added to tests/r7rs_tests.scm, including a\nsanity check that a nested vector sub-pattern (the actually meaningful\ncase) still works correctly.\n\n466/466 (r7rs_tests.scm), 108/108 (test_cli.sh), 117/117 ctest (default\nbuild), 118/118 ctest (LLVM build), 70/70 (syntax_rules_tests.scm) --\nall with .scc caches cleared.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-02T21:17:12+10:00",
          "tree_id": "7185f7b31766f56d80c84a373d67d68d6df06edb",
          "url": "https://github.com/deconstructo/curry/commit/09337ea0ec83427bbd25ff98c13d7fbcd49bfe2d"
        },
        "date": 1788347878661,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.933,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 34.522,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.724,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 40.314,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 135.688,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 284.294,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.076,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.074,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 67.124,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "37c4c8ff873901eb579ca344e446835e89e96d6f",
          "message": "fix(symbolic): memoize sx_simplify to close O(depth^2) CPU-exhaustion DoS (#137) (#142)\n\n* fix(symbolic): memoize sx_simplify to close O(depth^2) CPU-exhaustion\n\nFixes #137: sx_simplify re-walked and re-simplified its entire argument\ntree from scratch on every call, even subexpressions a prior call\nalready fully simplified. Building a symbolic expression by repeatedly\nwrapping an already-simplified result one level at a time (e.g. `(sin\ne)` called N times in a loop, each call passing in the previous\niteration's own result) cost O(depth^2) total work, not O(depth) --\nunder a generous stack ulimit this alone burned 20-30+ seconds of CPU\nbefore #134's stack-depth guard ever engaged, a genuine DoS distinct\nfrom that fix.\n\nClosed with a generation-tagged memoization cache: SymExpr's own\nhdr.flags field (otherwise unused for this type) stores the\ng_sx_simplify_generation value in effect the last time a given node\nwas fully simplified. sx_simplify (now a thin public wrapper around\nthe renamed sx_simplify_impl) checks that tag first and returns the\nnode unchanged with no recursion at all if it matches the current\ngeneration. Every recursive call inside sx_simplify_impl's own 700-line\nbody still calls the public sx_simplify, not the impl directly, so an\nalready-tagged subexpression short-circuits at ANY depth, not just at\nthe top level -- turning the \"wrap an already-simplified result\"\npattern back into O(depth) total.\n\nThe generation counter exists because simplification isn't a fixed\nfunction of shape alone: define-rule/define-algebra register new\nrules/algebra properties at runtime, which can change what \"fully\nsimplified\" means for operators already in use. A bare one-bit tag\nwould let a node cached before a new rule was registered keep being\nserved stale after the registration -- a real correctness regression,\nnot just a missed optimization. Both sx_rule_add (sx_rules.c) and\nsx_algebra_define (sx_algebra.c) now call the new\nsx_invalidate_simplify_cache() (symbolic.h) on every successful\nregistration, bumping the counter so every previously-cached tag stops\nmatching.\n\nAlso updated tests/test_cli.sh's own #134 regression test: its\n\"wrap in a loop\" construction no longer deepens the real C stack at\nall after this fix (each wrap now touches only the one new outer node),\nso it could no longer reach that guard regardless of how large N got.\nSwitched to `up`-tuple nesting (O(1) per level, no simplification pass,\nthe same construction r7rs_tests.scm's own #134 test for sx_depends_on/\n∫ already uses) consumed by `∂` (sx_diff has no memoization of its own\n-- this fix was scoped to sx_simplify only), which still recurses the\nreal C stack once per level and reaches the guard reliably.\n\n4 new regression tests added to tests/sx_algebra_tests.scm: a\n200000-deep (sin (sin ...)) chain builds correctly and re-simplifying\nit returns the identical object (proving the memoization fast-path\nfires), plus a cache-invalidation correctness test confirming a rule\nregistered AFTER a node was cached still fires on that cached node.\n\n466/466 (r7rs_tests.scm), 108/108 (test_cli.sh), 53/53 (sx_algebra_tests.scm,\nup from 49), 406/406 (numeric_ext_tests.scm), 117/117 ctest (default\nbuild), 118/118 ctest (LLVM build) -- all with .scc caches cleared.\nEmpirically confirmed the fix: a 100000-deep wrap chain that previously\nwould have taken tens of seconds (or hit the stack-depth guard first,\ndepending on ulimit) now completes in ~0.04s.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\n\n* fix(symbolic): close review findings on sx_simplify memoization (#137)\n\nFollow-up to 8a4dc28, addressing findings from independent code review\nand security review of that commit:\n\n- sx_simplify's cache is invalidated on define-rule/define-algebra\n  registration, but simplification results also depend on SymVar\n  assumption flags (e.g. sqrt(x^2) -> |x| vs x). A node cached before\n  an assume!/drop-assumption!/with-assumptions change could be served\n  stale afterward. Now invalidated from every entry point that mutates\n  a SymVar assumption flags: eval.c S_WITH_ASSUMPTIONS (entry, normal\n  exit, and exception re-raise paths) and builtins_curry.c\n  prim_assume/prim_drop_assumption/prim_assumption_set/\n  prim_assumption_restore (also covers the compiled with-assumptions\n  path, which desugars to the same primitives).\n\n- sx_rules_clear (rule removal) did not invalidate the cache, only\n  sx_rule_add (rule addition) did. A node cached as a fixpoint under a\n  since-removed rule could keep being served stale. Fixed by\n  invalidating on both directions.\n\n- The generation counter could wrap to 0, which sx_make_expr zero-\n  initialized hdr.flags would then read as \"already simplified\" for a\n  node that was never simplified at all. Fixed by skipping 0 on\n  wraparound. (A residual concern -- wrapping to some other previously\n  used generation value, not just 0 -- is being tracked separately,\n  see issue filed below.)\n\n- Curry actors are real OS threads, so the generation counter and each\n  node cached tag are potentially accessed concurrently. Switched to\n  __atomic_load_n/__atomic_add_fetch on the plain (non-_Atomic)\n  uint32_t counter, matching this codebase established convention for\n  lock-free counters elsewhere (see runtime.c).\n\n- test_cli.sh #134 regression test used N=3000 under ulimit -s 2048,\n  verified only against a Debug build. Security review found a Release\n  build (smaller per-recursion-level stack frames under optimization)\n  does not reach the guard at that depth. Re-measured directly against\n  both Debug and Release builds here: N=3000 overflows in Debug only,\n  N=20000 in both; bumped to N=50000 for margin (still under 0.1s).\n\nFull suite re-verified after these changes: 117/117 ctest suites pass\n(fresh --clear-cache run), including cli and sx_algebra.\n\nTwo residual findings from the security review are not addressed here\nand are being filed as a separate follow-up issue instead of folded\ninto this already-large fix: (1) sx_invalidate_simplify_cache bumps\none global counter, so interleaving cheap rule/algebra registrations\nwith expression construction can defeat memoization entirely,\nreintroducing O(depth^2)-comparable cost; (2) the generation counter\n32-bit range can in principle wrap around to collide with a stale\nper-node tag still in memory, not just to 0. Both point at the same\nunderlying limitation -- a single global generation number cannot\nscope cache validity to just the affected operator or subtree -- which\nneeds its own design pass (e.g. per-operator generation tracking)\nrather than a hurried bit-packing fix appended here.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n* fix(symbolic): CAS-based wraparound skip and acquire/release ordering for sx_simplify cache (#137)\n\nSecond independent code review of f3d91b0 found two remaining gaps in\nthe generation-counter mechanism itself (distinct from the two\nalready-filed issue #140 findings about cross-operator granularity and\ngeneral non-zero collision):\n\n- The \"skip 0 on wraparound\" fix was two separate atomic RMW ops (a\n  fetch-add, then a corrective second fetch-add if the result was 0).\n  That leaves a real window, right when the counter lands on 0 after\n  wraparound, during which another threads concurrent load can observe\n  0 before the corrective add runs -- reintroducing the exact\n  \"never-simplified node misread as cached\" bug the skip exists to\n  prevent (sx_make_expr zero-initializes hdr.flags). Fixed with a\n  compare-exchange retry loop, so the increment and the skip-0 step are\n  one atomic operation and no other thread ever observes an\n  intermediate 0.\n\n- Every access used __ATOMIC_RELAXED, which guarantees no torn reads of\n  the counter but establishes no happens-before edge between \"a rule/\n  algebra/assumption mutation just happened\" (an ordinary, non-atomic\n  store) and \"another actor thread observes the resulting generation\n  bump.\" On a weakly-ordered architecture (this codebase explicitly\n  targets arm64 -- Apple Silicon and the Docker arm64 Ubuntu/Fedora\n  checks in CLAUDE.md), that store could be reordered past the atomic\n  bump, letting a thread see the new generation while still reading\n  stale assumption flags or rule-table state. Switched to\n  __ATOMIC_RELEASE on the bump and __ATOMIC_ACQUIRE on the load,\n  matching runtime.cs g_jit_arith_tainted, which already uses\n  acquire/release rather than relaxed for the identical cross-thread-\n  visibility reason.\n\n117/117 ctest suites pass (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-03T19:29:59+10:00",
          "tree_id": "1563cc1283c424e2deccb6e53d86583ddea6abf5",
          "url": "https://github.com/deconstructo/curry/commit/37c4c8ff873901eb579ca344e446835e89e96d6f"
        },
        "date": 1788427837351,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.322,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.919,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.866,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 37.744,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 105.617,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 248.268,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 61.208,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 73.293,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 55.964,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "aa9c35bf2f46a23a152c2a4147d37b13f840339e",
          "message": "fix(symbolic): synchronize sx_rules/sx_algebra tables against concurrent actor access (#141) (#147)\n\n* fix(symbolic): synchronize rtab/atab against concurrent actor access (#141)\n\nsx_rules.c's rule table (rtab) and sx_algebra.c's algebra table (atab)\nwere read and written with no synchronization at all, despite curry\nactors being real OS threads with no global interpreter lock. Found\nduring the independent security review of #137 (sx_simplify\nmemoization): that fix hardened the memoization generation counter\nitself with real atomics specifically because a script can run\ndefine-rule/define-algebra on one actor while another is mid-simplify\n-- but the same argument applies with more force to the tables that\nbookkeeping is meant to track, and those were left completely\nunguarded.\n\nConcretely, before this fix:\n- sx_rule_add appended to a rule chain via an unsynchronized\n  walk-to-tail then write -- two concurrent registrations for the same\n  operator could compute the same stale tail and race to write ->next,\n  silently losing one registration.\n- sx_algebra_define wrote AlgebraInfo's five fields into a shared slot\n  one at a time with no barrier -- a concurrent reader could observe a\n  torn combination of old and new fields (e.g. a new `commutative`\n  paired with a stale `relations_fn`).\n- sx_rules_clear unlinked list nodes concurrently with sx_rule_try's\n  unsynchronized traversal of the same list.\n\nFixed with a pthread_rwlock_t per table (single-writer/many-reader,\nsince mutation via define-rule/define-algebra/clear-rules! is rare next\nto lookups on every sx_simplify call for a user-defined operator):\n\n- sx_rule_add/sx_rules_clear (writers) and sx_rules_list (reader) hold\n  rtab_lock for their full critical section.\n- sx_rule_try snapshots the matching rule chain's needed fields\n  (pattern/pvars/guard_fn/action_fn) into a local array under the read\n  lock, then releases the lock BEFORE calling sx_pattern_match/guard_fn/\n  action_fn. This matters because guard_fn/action_fn are arbitrary\n  Scheme callables -- one could itself call define-rule, and\n  pthread_rwlock_t is not recursive, so still holding the read lock\n  while sx_rule_add tried to take the write lock on the same thread\n  would self-deadlock.\n- sx_algebra_lookup's signature changed from returning a raw pointer\n  into the live table (AlgebraInfo *) to copying a whole-struct snapshot\n  into an out-parameter under the read lock (bool sx_algebra_lookup(op,\n  AlgebraInfo *out)), for the same reason: the caller in symbolic.c\n  reads several of the struct's fields across a stretch of code that\n  also invokes relations_fn (another arbitrary Scheme callable), so a\n  raw pointer into the table would let a concurrent sx_algebra_define\n  mutate the very struct being read mid-read.\n\nAdded a regression test (tests/sx_algebra_tests.scm) spawning several\nactors doing concurrent define-rule/define-algebra/simplify calls,\nverifying this completes without a crash or hang -- the race being\nclosed is a lost-update/torn-struct correctness hazard, not a\nmemory-safety one (confirmed by the security review: no crash in a\n50000-iteration 4-actor stress test even before this fix), so this test\nchecks the locking itself doesn't introduce a NEW hazard (deadlock),\nrather than asserting a specific simplification outcome.\n\n117/117 ctest suites pass (fresh --clear-cache run; the one apparent\nfailure in a full parallel run, websocket, is the existing known-flaky\nport-bind test, confirmed passing in isolation, unrelated to this\nchange).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n* test(symbolic): add deterministic reentrant-callback regression for #141's locking\n\nIndependent code review of def4f46 found that the concurrency stress\ntest added alongside it never actually exercises the one hazard the\nlocking design specifically guards against: sx_rule_try (and\nsx_algebra_lookup's caller) must release rtab_lock/atab_lock BEFORE\ninvoking a rule's guard_fn/action_fn or an algebra's relations_fn,\nsince those are arbitrary Scheme callables that could themselves call\ndefine-rule/define-algebra, and pthread_rwlock_t is not recursive -- a\nthread still holding even a read lock when it tried to take the write\nlock on the same lock would self-deadlock. The multi-actor stress test\nonly ever registered non-reentrant rules, so it could not have caught\na regression that moved the unlock later.\n\nAdded two deterministic, single-threaded cases: an action_fn that\ncalls define-rule from inside sx_rule_try, and a relations_fn that\ncalls define-algebra from inside sx_algebra_lookup's caller. Both\nconfirm no self-deadlock and that the reentrantly-registered rule/\nalgebra actually takes effect afterward. Unlike the stress test, these\nfail reliably (not just \"sometimes,\" under the right interleaving) if\na future change reintroduces the hazard.\n\nAlso filed issue #143 for an unrelated, pre-existing gap the same\nreview surfaced while checking the \"stop-the-world makes rtab/atab's\nunsynchronized GC-scan callbacks safe\" reasoning: those scan callbacks\nare registered with the semispace GC backend, which is unreachable\ndead code (no CLI flag selects it), while the one real moving\ncollector (--gc generational) has no scanner registered for rtab/atab\nat all. Not addressed here -- out of scope for a locking fix, and\nunrelated to the default Boehm backend both #137 and #141 were\nverified against.\n\n117/117 ctest suites pass (fresh --clear-cache run; websocket's usual\nflake reproduces in a full parallel run and passes in isolation, as\nestablished in prior sessions).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n* fix(symbolic): lock rtab/atab's GC ext-scanners against the live generational backend (#141)\n\nIndependent security review of the rtab/atab locking fix (def4f46)\nfound that sx_rules_gc_scan/sx_algebra_gc_scan -- registered via\ngc_ss_register_ext_scanner in sx_rules_init/sx_algebra_init -- are NOT\ndead code tied to an unreachable semispace backend, as originally\nassumed while investigating a related question during that same\nreview. gc_ss_register_ext_scanner (src/gc.h:298) is a static inline\nalias straight through to the real gc_register_ext_scanner (src/gc.c),\nwhich IS wired into the actually-shipped --gc generational backend\n(src/gc_gen.c). The review captured a live stack trace of\ngc_gen_minor_collect -> sx_rules_gc_scan running concurrently with\nother actor threads mid-stress-test, confirming this scanner runs for\nreal and mutates rtab/atab's fields with zero synchronization against\nthe rwlocks def4f46 had just added -- because gc_gen.c's minor\ncollection is explicitly per-thread, not a stop-the-world pause for\nother actors (see that file's own comment on this), any actor's own\nallocation can trigger a minor collection whose scanner races against\nevery other actor's rtab_lock/atab_lock-protected access to those same\ntables. This is the same tables def4f46 was written to protect, so\nfixed here rather than filed as a fully separate issue.\n\nFix: both scanners now take their table's write lock for the duration\nof the scan. This is safe from self-deadlock only because every OTHER\nplace that takes rtab_lock/atab_lock in this pair of files was already\n(or is now) guaranteed to never allocate while holding it -- otherwise\na thread's own GC_MALLOC while it already held a read lock could\ntrigger the very minor collection whose scanner then tries to take the\nwrite lock on that same thread, deadlocking against itself. Two call\nsites needed restructuring to actually guarantee this:\n\n- sx_rule_try (the hot path, called from every sx_simplify on a\n  user-defined operator) was GC_MALLOC-ing its rule-chain snapshot\n  while holding rtab_lock for reading. Switched to a fixed on-stack\n  array (SX_RULE_TRY_MAX = 256 rules per operator, matching this\n  file's existing \"silently cap and move on\" convention for pathological\n  inputs), which needs no allocation at all.\n- sx_rules_list (list-rules) was calling scm_cons -- which allocates --\n  while holding rtab_lock for reading. Restructured to a count-under-\n  lock, allocate-outside-lock (with headroom for concurrent growth),\n  copy-under-lock, build-the-result-list-outside-lock sequence, so the\n  only allocation happens with no lock held at all.\n\nsx_algebra_define/sx_algebra_lookup already never allocated while\nholding atab_lock, so sx_algebra_gc_scan's fix needed no matching\nrestructuring elsewhere.\n\nVerified directly against the actual hazard: ran the existing 7-actor\nconcurrency stress pattern (writers/readers/algebra-writer/list-rules/\ngc-hammer) under `--gc generational --gc-nursery-size 16K` (tight\nnursery to maximize minor-collection frequency) for several runs with\nno hang or crash -- this is the one test that would have caught the\noriginal gap, since the default Boehm backend (what all prior testing\nin this session used) never exercises these scanners at all.\n\nAlso corrected issue #143, filed earlier in this same effort under the\nmistaken premise (from a different review pass) that these scanners\nwere dead code: they are not, and this commit is the fix. Left #143\nopen, retitled, to track the one remaining unfixed instance of the\nsame pattern: modules.c's scan_module_registry mutates the module\nregistry the same unsynchronized way under the same live scanner path,\nbut that is a different subsystem/data structure, out of scope for a\nrtab/atab-focused fix.\n\n117/117 ctest suites pass (fresh --clear-cache run, default Boehm\nbackend).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n* fix(symbolic): fix sx_rule_try's fixed-256 rule cap dropping legitimate rules (#145)\n\nIndependent security review of 54bb5a2 (the GC ext-scanner locking fix)\nfound that its SX_RULE_TRY_MAX = 256 fixed on-stack snapshot array --\nadded specifically to avoid allocating while holding rtab_lock for\nreading, since that allocation could otherwise trigger a minor GC\nwhose scanner then self-deadlocks trying to take rtab_lock for writing\non the same thread -- silently and permanently dropped every rule\nregistered past the 256th for one operator, with no diagnostic.\nConfirmed via direct repro: registering 300 distinct guarded rules for\none operator, rules #257-300 never fired again after that commit,\nwhere they had before it (and should have).\n\nFixed by applying the same count/allocate-outside-lock/copy pattern\nsx_rules_list already uses for the identical self-deadlock hazard:\ncount the matching chain's length under a read lock (no allocation),\nrelease the lock, GC_MALLOC a buffer sized to that count plus 25%+8\nheadroom (to tolerate concurrent growth between the two lock\nsections) with no lock held, then re-acquire the read lock only to\ncopy the (now possibly-stale) chain into that buffer, bounded by the\nbuffer's actual capacity. This only truncates if an operator's rule\ncount grows by more than the headroom in the few-microsecond gap\nbetween the two lock sections -- the same accepted best-effort bound\nsx_rules_list already documents -- rather than a hard, easily-hit\n256-rule ceiling.\n\nAdded a regression test (tests/sx_algebra_tests.scm) registering 300\nguarded rules for one operator (built via eval, since define-rule's\npattern/guard are literal at macro-expansion time) and confirming\nrules #42, #257, and #299 all still fire correctly.\n\nAlso investigated a separate, more serious finding from the same\nreview: a reproducible SIGSEGV / heap corruption under\n`--gc generational` with heavy concurrent rtab/atab traffic (filed as\nissue #144). Bisected by running the exact same repro against\n`main`@37c4c8f (before any of this branch's locking work): it does not\nSIGSEGV there, but the same script does deterministically fail with a\ntype error (\"apply: not a procedure\") under `--gc generational` where\nit completes normally under the default Boehm backend on that same\ncommit -- confirming the underlying corruption predates #137/#141\nentirely and is unrelated to rtab_lock/atab_lock, just manifesting\ndifferently depending on what gets corrupted. Left #144 open and\nupdated with this finding for separate follow-up; not blocking this\nbranch.\n\nAlso left issue #146 open and unaddressed here (sx_rules_gc_scan/\nsx_algebra_gc_scan now pay O(table size) under a write lock on every\nminor GC, a real throughput cliff under --gc generational as rtab/atab\ngrow) -- fixing that properly needs an incremental/remembered-set-style\nscanning scheme, real architecture work out of scope for a locking fix,\nand --gc generational is documented experimental, not the default\nbackend.\n\n117/117 ctest suites pass (fresh --clear-cache run, default Boehm\nbackend).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-03T21:25:20+10:00",
          "tree_id": "54c25491c2cb7101984984513716ded7118a72ec",
          "url": "https://github.com/deconstructo/curry/commit/aa9c35bf2f46a23a152c2a4147d37b13f840339e"
        },
        "date": 1788434768336,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.133,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 26.314,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.144,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 31.037,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 149.981,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 286.859,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 68.906,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 92.285,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 73.308,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "613a6cdcaea0bf7b888d0abe8408d39c89c21443",
          "message": "fix(modules): synchronize the module registry against concurrent actor access (#143) (#151)\n\nmodules.c's module registry (a name-list -> Module* linked list backing\nevery `import`) was read and written with zero synchronization, despite\ncurry actors being real OS threads with no global interpreter lock.\nFound during review of #141's identical fix for sx_rules.c's rtab /\nsx_algebra.c's atab: registry_insert's prepend\n(\"e->next = registry; registry = e;\") is an unsynchronized read-modify-\nwrite on the shared head pointer -- two concurrent inserts can both\nread the same old head, and whichever write loses silently drops that\nentry from the list forever, the same lost-update sx_rule_add's\nidentical unsynchronized append had before #141.\n\nFixed with a pthread_rwlock_t (module_registry_lock), single-writer/\nmany-reader, matching #141's design:\n- registry_insert (writer) allocates its ModuleEntry BEFORE taking the\n  lock (not while holding it), since gc_alloc_raw_pinned is itself an\n  allocation that under --gc generational can trigger a minor GC whose\n  ext scanner (scan_module_registry, below) takes this same lock for\n  writing -- allocating while already holding it would self-deadlock,\n  the identical hazard #141 closed for sx_rule_try/sx_rules_list.\n- registry_lookup (reader) holds the lock for its whole traversal --\n  unlike sx_rule_try, this never calls back into arbitrary Scheme code,\n  so no snapshot-then-release pattern is needed here.\n- scan_module_registry (the GC ext-scanner, confirmed live under\n  --gc generational by the same investigation that found #141's\n  scanners were, not dead code) now takes the write lock for its scan,\n  for the identical reason sx_rules_gc_scan/sx_algebra_gc_scan needed\n  to.\n\nUnlike rtab/atab, this registry has no removal function at all\n(modules are never unloaded), so there's no unlink-vs-traversal hazard\nto close -- the fix here is narrower than #141's.\n\nAdded a concurrency regression test (tests/module_isolation_tests.scm):\n4 actors concurrently importing modules, asserting completion without\ncrash or hang. Deliberately Boehm-only: the same script (concurrent\nimport, no sx_rules/sx_algebra involved at all) was separately found to\ntrip an unrelated, pre-existing corruption bug under\n`--gc generational` -- confirmed to reproduce identically on\nmain@aa9c35b, i.e. without this fix, so it's a wider-reaching instance\nof already-filed issue #144, not something this test is meant to catch\nor something this fix could plausibly cause.\n\n117/117 ctest suites pass (fresh --clear-cache run, default Boehm\nbackend).\n\n\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-03T23:45:47+10:00",
          "tree_id": "57e7a0009a8477b52a33f91c009894b2dda30a80",
          "url": "https://github.com/deconstructo/curry/commit/613a6cdcaea0bf7b888d0abe8408d39c89c21443"
        },
        "date": 1788443189663,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.257,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.089,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.992,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 24.327,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 118.707,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 228.328,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 53.784,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 73.323,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 58.182,
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
          "id": "3b515c1108fbfa1023ea9e85e3fb8a4473f959e5",
          "message": "Release v1.23.6",
          "timestamp": "2026-09-03T23:52:19+10:00",
          "tree_id": "a89b27c60c6bca11fd9317ba950973a70ab769d6",
          "url": "https://github.com/deconstructo/curry/commit/3b515c1108fbfa1023ea9e85e3fb8a4473f959e5"
        },
        "date": 1788443620751,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.028,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 31.237,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.823,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 35.356,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 140.901,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 294.612,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 69.911,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 91.612,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 67.749,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "e358a2db6c90ecf0a1ffd9a71fb06b6a03310691",
          "message": "fix(modules,env): synchronize modules_import's no-export-list walk against concurrent env_define (#148) (#154)\n\n* fix(modules,env): synchronize modules_import's no-export-list walk against concurrent env_define (#148)\n\nmodules_import's \"no export list\" branch (re-exporting every binding in\nan imported module when the module declared no explicit (export ...)\nclause) walked the module's EnvFrame chain directly:\n\n    EnvFrame *f = mod->env;\n    while (f) {\n        for (uint32_t i = 0; i < f->size; i++)\n            import_binding(f->syms[i], f->vals[i], spec, filter, env);\n        f = f->parent;\n    }\n\nmod->env (and GLOBAL_ENV, which every (scheme base)/(scheme write)/etc.\nalias directly with has_exports = false) is a root frame -- exactly the\nkind env.c's own seqlock protocol exists to protect, since it can still\nbe mutated by another actor concurrently defining new bindings into it\n(frame_define -> frame_grow/frame_hash_rehash reallocate f->syms/\nf->vals/f->hidx and bump f->size with no synchronization of their own,\nrelying entirely on frame_lookup/frame_set going through the seqlock\nretry protocol instead). This raw walk bypassed that protocol entirely,\nreading f->size/f->syms/f->vals with no synchronization at all.\n\nIndependent code review of #143 (an unrelated fix to modules.c's module\nregistry) caught this with ThreadSanitizer: four distinct data races\nreported, e.g. a read at modules.c:547 racing a write inside\nframe_define_unlocked (env.c) from another actor's concurrent\ntop-level define. Confirmed by this fix: the same TSan repro (import\n(scheme base) from two actors while two others concurrently grow\nGLOBAL_ENV with fresh defines) reproduces the modules.c:547 race on the\npre-fix code and does not after.\n\nFixed by adding frame_snapshot_bindings (env.c/env.h): the same\nseqlock-validated read frame_lookup_versioned already does for a single\nsymbol lookup, generalized to copy out an entire frame's (sym, val)\npairs in one consistency-checked pass -- retrying if a concurrent\nframe_define was in progress or completed mid-copy, for root frames;\na plain memcpy for every other (single-thread-owned) frame, matching\nframe_lookup_versioned's own fast path. modules_import's no-export-list\nwalk now calls this once per frame instead of indexing f->syms/f->vals\ndirectly.\n\nAdded a regression test (tests/module_isolation_tests.scm): two actors\nimporting (scheme base) while two others concurrently define fresh\ntop-level bindings, forcing real frame_grow/rehash calls mid-import.\nThe existing #143 concurrency test (srfi 1/128) never actually\nexercised this code path, since SRFI libraries all declare explicit\nexport lists -- only the has_exports = false path (C modules, plain\n.scm files without define-library, and the built-in (scheme *)\naliases) is exposed to this race.\n\n117/117 ctest suites pass (fresh --clear-cache run). Also verified\ndirectly under ThreadSanitizer (not part of the normal build): the\nmodules.c:547 race is present on the pre-fix code and absent after,\nconfirmed by comparing against a stashed baseline build. The remaining\nTSan warnings under this same workload (frame_grow/frame_hash_rehash/\nhash_insert/gc_wb_slot in env.c) are identical before and after this\nfix -- they are the seqlock protocol's own accepted, by-design\n\"benign races\" (env.c's existing header comment already documents\nthis tradeoff), not something this commit introduces or could\neliminate.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n* docs(modules): correct #148 comment on why the no-export-list walk needed locking\n\nIndependent security review of 71312ec found the comment (and the\noriginal commit message) described the wrong triggering scenario: it\nclaimed a define-library that self-registers before its own body\nfinishes running could expose a partially-loaded module to a\nconcurrent importer. Tracing modules_define_library and\nmodules_define_r6rs_library shows registry_insert only ever runs\nafter a define-library body whole clause-processing loop completes --\nthat specific path does not currently expose a partial module.\n\nThe real and sufficient hazard is simpler: every (scheme base),\n(scheme write), etc. alias IS GLOBAL_ENV directly\n(modules_register_builtin), and a plain .scm/.sld module loaded\nwithout define-library gets mod->env = env_extend(GLOBAL_ENV), so the\nparent-chain walk reaches GLOBAL_ENV either way. GLOBAL_ENV is the\none frame actors genuinely share, and any other actor's ordinary\ntop-level define races this walk with no relationship between the\ntwo beyond both running concurrently -- exactly what the regression\ntest in tests/module_isolation_tests.scm exercises. Corrected the\ncomment so a future reader traces the actual live hazard, not a\nhypothetical one.\n\nNo functional change.\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-04T00:34:00+10:00",
          "tree_id": "597fd384d506019ff9f43f8d001e93b5c7f1c03a",
          "url": "https://github.com/deconstructo/curry/commit/e358a2db6c90ecf0a1ffd9a71fb06b6a03310691"
        },
        "date": 1788446093928,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.134,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.224,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.12,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.865,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 127.858,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 284.482,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 68.324,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 89.265,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 65.685,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "76ede5f3ce8fdaa960ce2540d486f0c452b88807",
          "message": "fix(main): synchronize REPL ,env against concurrent GLOBAL_ENV mutation (#152) (#155)\n\n* fix(main): synchronize REPL ,env against concurrent GLOBAL_ENV mutation (#152)\n\nThe REPL's ,env command (main.c) read GLOBAL_ENV's frame->size/\nframe->syms directly with no synchronization -- the same bug pattern\nissue #148 fixed in modules_import's no-export-list import walk.\nGLOBAL_ENV is a root frame actors can mutate concurrently via ordinary\ntop-level (define ...) calls even while the REPL sits idle at its\nprompt, racing frame_define's unsynchronized frame_grow/\nframe_hash_rehash (which reallocate f->syms/f->vals/f->hidx and bump\nf->size with no synchronization of their own, relying entirely on\nreaders going through env.c's seqlock protocol instead). Found by\n#148's own code reviewer while auditing the codebase for other\nunfixed instances of the same pattern.\n\nFixed by switching ,env to frame_snapshot_bindings (env.c/env.h), the\nsame whole-frame seqlock-validated copy #148 introduced -- values\naren't needed here, only names, so the paired vals array from the\nsnapshot goes unused.\n\nAdded a regression test to tests/test_cli.sh: spawns two actors\ncontinuously defining fresh top-level globals via eval, then issues\n,env as the very next stdin line (not after waiting for the actors to\nfinish, which would defeat the point), verifying the REPL survives\nwithout crashing or hanging while genuinely racing the actors --\nconfirmed by inspecting the interleaved output, which shows ,env's\nlisting running while the actors were still mid-loop (iteration\ncounts well short of their target), not after they'd already\nfinished.\n\n117/117 ctest suites pass (fresh --clear-cache run, verified across\nthree full-suite runs for confidence after one anomalous 5-minute\ntimeout on an initial run turned out not to reproduce -- the cli\nsuite, which carries this new test, consistently finished in ~25s\nacross all three follow-up runs).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n* test(cli): add ctest TIMEOUT for the cli suite\n\nIndependent code review of #152's ,env fix (ce3d22c) reproduced the\nanomalous full-suite timeout mentioned in that commit's own message\nonce across 9 runs (could not pin down further diagnostics -- the\nfailure was not reproducible on demand, matching the original\nauthor's own account) and pointed out a real gap: the cli ctest entry\nhad no TIMEOUT property, unlike actors/websocket/ros, which already\ncarry one specifically because of this exact \"rare, non-deterministic\nhang\" risk shape (see tests/CMakeLists.txt's existing comments on\nthose). The new ,env regression subtest #152 added to cli races two\nactors against a busy-wait poll loop whose termination depends on\nboth actors actually finishing -- the identical open-ended-hang shape,\nnow inside a test suite that previously had no isolated timeout of\nits own. A real hang here would otherwise block only up to CI's\n30-minute job-level timeout with no fast, diagnosable ctest-level\nsignal.\n\nAdded set_tests_properties(cli PROPERTIES TIMEOUT 300) -- generous\nheadroom above cli's normal runtime (observed up to ~70s in CI,\n~25-27s locally across repeated runs) while still turning a future\nrecurrence into a fast, clearly-labeled TIMEOUT instead of an\nopen-ended block, matching the existing convention for actors/\nwebsocket/ros.\n\n117/117 ctest suites pass (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n* test(cli): fix flaky check_contains usage in #152's ,env regression test\n\nIndependent security review of ce3d22c found the new ,env regression\ntest flaky on macOS at roughly 1-in-4 to 1-in-5 runs, contradicting\nthis branch's own earlier \"three follow-up runs, all passing\" claim --\nthe author's local run count just hadn't happened to hit it yet.\nRoot-caused by the reviewer: check_contains's printf '%s' \"$haystack\"\n| grep -qF pattern is timing-sensitive once $haystack is large (tens\nof KB, in this case curry's full GLOBAL_ENV symbol listing including\nmany multi-byte Akkadian/cuneiform names) and piped through macOS's\nshipped BSD grep 2.6.0-FreeBSD (~2010) under -q's early-exit-on-first-\nmatch behavior. Confirmed NOT a curry bug: byte-level inspection\nduring every observed failure showed the searched-for string was\nalways present, byte-for-byte, in curry's actual output -- replaying\nthe identical captured bytes through the same grep command afterward\nalways succeeded.\n\nFixed by adding check_contains_file (writes curry's output straight\nto a file instead of routing it through a shell variable and a pipe,\nthen greps the file directly), and switching the #152 test to use it.\nVerified with 18 consecutive full test_cli.sh runs (0 failures), where\nthe pre-fix version was expected to fail roughly 1 in 4-5 runs per the\nreviewer's own reproduction rate.\n\nAlso verified the earlier \"anomalous timeout\" concern doesn't\nreproduce as a genuine single-run hang: an isolated test_cli.sh run\ncompleted in under 30s with both #152 checks passing, confirming a\nprior batch-loop \"run 13 appears stuck\" observation was just the\nouter polling tool's own 5-minute cap being hit by CUMULATIVE runtime\nacross many sequential ~25-30s runs, not any single run hanging.\n\n117/117 ctest suites pass (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-04T01:43:13+10:00",
          "tree_id": "64839ca0cffc8bd0dda3c5e759ace61ecb7ba8b3",
          "url": "https://github.com/deconstructo/curry/commit/76ede5f3ce8fdaa960ce2540d486f0c452b88807"
        },
        "date": 1788450259674,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 12.345,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.219,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.284,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 25.523,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 91.96,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 220.126,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 55.3,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 61.727,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 44.533,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "47bc4b8c980b0f6aac5a16f096e295fef4a885f4",
          "message": "fix(modules): re-check registry before inserting a freshly-loaded C module (#149) (#157)\n\nmodules_try_load's .so/.dylib branches inserted a freshly loaded\nModule into the registry unconditionally, unlike the .sld/.scm\nbranches just below them, which already re-check the registry after\nloading and prefer whatever is there before inserting (originally to\nhandle a define-library body self-registering during its own load,\nnot specifically for cross-actor concurrency -- but the mechanism is\nagnostic to WHO registered first, so it incidentally closes the\nconcurrency race too).\n\nFound during independent security review of #143 (module registry\nlocking): two actors racing to first-import the same not-yet-loaded\nC module could both see registry_lookup return NULL, both proceed to\nload_c_module (each independently dlopen-ing and running\ncurry_module_init), and both registry_insert their own Module*.\nWhichever insert ran last won as the identity every future lookup\nresolves to, while the OTHER racing importer kept and used the\nModule* it personally created -- two actors ending up bound to two\ndifferent \"singleton\" module instances.\n\nFixed by applying the identical re-check-then-prefer-existing pattern\nalready used for .sld/.scm to the .so/.dylib branches. This does not\nundo curry_module_init having run twice if the race was already lost\nby the time the re-check happens -- that side effect already\noccurred, and is a known, accepted consequence matching the .sld/.scm\npath's own existing duplicate-load tolerance -- but it does make\nevery racing actor converge on the same Module* going forward, closing\nthe observable inconsistency.\n\nAdded a regression test (tests/module_isolation_tests.scm): 16 actors\neach doing exactly one first-ever import of a not-previously-loaded\nC module (curry json), maximizing simultaneous first-load race-window\ncontention, verifying no crash/hang and that the module actually\nworks afterward regardless of which racing actor's load won.\n\n117/117 ctest suites pass (fresh --clear-cache run).\n\n\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-04T19:15:15+10:00",
          "tree_id": "dba29e7b6d364b3ed697536534b665c3c1b3b967",
          "url": "https://github.com/deconstructo/curry/commit/47bc4b8c980b0f6aac5a16f096e295fef4a885f4"
        },
        "date": 1788513361323,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.284,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 20.133,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.957,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 24.357,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 117.924,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 224.051,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 52.614,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 74.026,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 57.121,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "c18200e4b0091482b6a19d17fe4df56ffa042db4",
          "message": "fix(network,websocket): bind ephemeral ports in websocket/websocket_server/ros tests (issue #110) (#159)\n\nThe websocket, websocket_server, and ros ctest suites each bound\nfixed, hardcoded port numbers (17995, 17996, 17988, 17987, 17986,\n17990), inherently vulnerable to collision under ctest -j parallel\nexecution against another process already bound to the same port --\na lingering listener from a previous run, a system service, or\nanother test. Issue #110 diagnosed this after a CI run where\nwebsocket hung completely silent for 28+ minutes (no timeout on the\nunderlying accept call) while ros, in the same run, failed fast and\ncleanly with \"tcp-listen: bind failed on port 17995\". A ctest-level\nTIMEOUT was added at the time as an immediate mitigation, explicitly\nnot a fix for the root cause, with \"bind to port 0 for an OS-assigned\nephemeral port\" identified as the actual long-term fix.\n\nThis closes that gap at the root instead of only bounding its\nsymptom. tcp-listen and SRFI-106's make-server-socket already accept\n0 to request an OS-assigned ephemeral port -- what was missing was a\nway to read back which port the OS actually picked, since bind()\nalready happened by the time the caller could ask. Added:\n\n- socket-local-port (modules/network/srfi106.c): a new primitive\n  querying a socket's bound local port via getsockname. Not part of\n  the SRFI-106 spec (that spec has no way to query a bound socket's\n  local address at all), so registered directly under (curry network)\n  rather than added to (srfi s106 sockets)'s own export list, keeping\n  that file's stated spec-faithfulness intact. Works on any socket\n  handle from tcp-listen, udp-socket, or SRFI-106's own\n  make-client-socket/make-server-socket/socket-accept, since they all\n  share the same underlying (socket . bytevector-packed-fd)\n  representation (network_internal.h).\n- ws-listener-port (lib/curry/modules/curry/websocket.scm): the\n  equivalent for a ws-listen listener, built on socket-local-port\n  applied to the listener's own socket.\n\nAll three test files now call (tcp-listen 0)/(ws-listen 0) and derive\ntheir actual port from socket-local-port/ws-listener-port immediately\nafterward, instead of using a literal port number anywhere.\n\nVerified the fix actually closes the failure mode it targets, not\njust \"still passes normally\": held all six of the OLD hardcoded ports\nbusy from an external process (exactly the CI collision scenario\nissue #110 described) and confirmed all three suites still pass\ncleanly, since none of them depend on any fixed port anymore. Also\nran the full parallel ctest suite (the actual failure-triggering\ncondition -- \"under ctest -j full-suite contention\") three times\nclean.\n\nThe ctest-level TIMEOUT on these three tests is kept as defense-in-\ndepth (matching the \"actors\" test's own identical treatment for an\nunrelated rare hang) -- even with the port-contention root cause\nclosed, an open-ended hang from some other cause is still better\ncaught as a fast, diagnosable TIMEOUT than a silent CI stall.\n\nDocumented socket-local-port (docs/reference/module-network.md) and\nws-listener-port (docs/reference/module-websocket.md).\n\n117/117 ctest suites pass (fresh --clear-cache run), including 3\nadditional full-parallel-suite runs and 15 additional standalone runs\nof the three affected test files, all clean.\n\n\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-04T19:20:22+10:00",
          "tree_id": "97dd676eda545d4d01f0c6af590afa6743c0a251",
          "url": "https://github.com/deconstructo/curry/commit/c18200e4b0091482b6a19d17fe4df56ffa042db4"
        },
        "date": 1788513669337,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 14.57,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 24.362,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 3.99,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.856,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 110.191,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 257.082,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 62.937,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 75.022,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 58.941,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "55214007fc89e83a9c16be17c48e9ed344b3ca64",
          "message": "fix(network): reject malformed raw socket handles across all raw-handle primitives (#158) (#163)\n\n* fix(network): reject malformed raw socket handles before net_val_to_sock (#158)\n\nnet_val_to_sock (modules/network/network_internal.h) unconditionally\nread sizeof(sock_t) bytes from a raw socket handle's bytevector with\nno bounds check of its own (curry_bytevector_ref does no bounds check\neither), while net_is_raw_socket_handle -- the gate deciding whether a\nvalue even reaches net_val_to_sock -- only checked that the value was\na pair whose car was the symbol 'socket, never that the cdr was\nactually a bytevector, let alone one of the right length.\n\nTwo distinct memory-safety issues followed from this:\n- A curry script constructing (cons 'socket (make-bytevector N ...))\n  with N < sizeof(sock_t) (including N=0) triggered a genuine\n  out-of-bounds heap read past the bytevector's own flexible-array-\n  member allocation, whose garbage result became an fd fed straight\n  into a real syscall (getsockname, send, recv, shutdown, close,\n  accept -- every primitive using net_extract_fd on a raw handle).\n- A cdr that wasn't a bytevector at all (e.g. (cons 'socket 42)) was\n  worse: curry_bytevector_length/curry_bytevector_ref assume their\n  argument already IS a bytevector (as_bytes does an unchecked cast),\n  so this was a type-confused read of whatever heap object the cdr\n  actually was, misinterpreted as a Bytevector's header/data.\n\nFound during independent security review of an unrelated commit\n(#110's ephemeral-port fix, which added one more caller of the same\nunsafe path via the new socket-local-port primitive).\n\nFixed by strengthening net_is_raw_socket_handle itself to verify both\nproperties before ever calling net_val_to_sock: the cdr must be an\nactual bytevector (curry_is_bytevector), and its length must be\nEXACTLY sizeof(sock_t) -- not just \"at least\", since exact match is\nthe only value that unambiguously round-trips through\nnet_sock_to_val's own construction. A malformed handle now falls\nthrough net_extract_fd's existing port-or-handle dispatch to\ncurry_port_fd, which already type-checks cleanly via vis_port and\nreturns -1 for any non-port value, producing the same \"not a socket\nhandle or file-backed port\" error net_extract_fd already raises for\nany other malformed input -- no new error path needed, just closing\nthe gap that let a malformed value slip past the type check meant to\ngate it.\n\nAdded a regression test (tests/network_tests.scm): too-short, empty,\nnon-bytevector, and too-long cdr values all rejected cleanly; a\ncorrectly-sized-but-garbage fd still reaches the syscall-level error\nit always did (a different, already-correct path this fix doesn't\nchange); and a genuine tcp-listen handle still works normally,\nconfirming the stricter check doesn't reject legitimate handles.\n\n117/117 ctest suites pass (fresh --clear-cache run, verified across\nmultiple full-suite runs).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n* fix(network): close the same OOB/type-confusion gap for tcp-accept/tcp-close/udp-* (#158)\n\nIndependent code review of d07456c found the fix was incomplete: it\nonly closed the gap for callers routing through net_extract_fd (every\nSRFI-106 socket-* primitive, plus socket-set-nonblocking!/socket-ready?\nin network.c itself). Five other network.c primitives -- tcp-accept,\ntcp-close, udp-bind, udp-send, udp-recv -- call net_val_to_sock\ndirectly via the raw #define val_to_sock alias, with no check of any\nkind, entirely bypassing net_is_raw_socket_handle's validation. Since\nthese are ordinary user-callable builtins taking arbitrary Scheme\nvalues, the review reproduced this concretely as an actual SIGSEGV\n(not merely a clean, catchable error) for:\n\n    (tcp-close (cons 'socket 42))\n    (udp-bind  (cons 'socket 42) 0)\n\nFixed by routing all five through a new shared helper,\nnet_checked_val_to_sock (network_internal.h): validates via the\nalready-fixed net_is_raw_socket_handle before ever calling\nnet_val_to_sock, raising a clean \"<who>: not a socket handle\" error\notherwise. Deliberately not net_extract_fd -- these five functions\nonly ever make sense on a raw handle, never a port (the same reasoning\nsrfi106.c's fn_socket_close already documents for its own identical,\nalready-correct inline check), so net_extract_fd's broader \"accepts\neither shape\" behavior would be the wrong fit here, same as it would\nbe for fn_socket_close.\n\nVerified all five previously-reproducible SIGSEGVs now produce clean,\ncatchable errors instead (tcp-close, udp-bind, tcp-accept, udp-send,\nudp-recv, each tested directly against a malformed handle). Added\nregression coverage for all five to tests/network_tests.scm; every\nother assertion in that file continues to exercise these same five\nprimitives with genuine tcp-listen/udp-socket handles, confirming the\nadded check doesn't reject legitimate use.\n\n117/117 ctest suites pass (fresh --clear-cache run).\n\nCo-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\n---------\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-04T20:15:22+10:00",
          "tree_id": "c6f18b1c8d1d507218a07f4fd7d399dfb5d4a4e1",
          "url": "https://github.com/deconstructo/curry/commit/55214007fc89e83a9c16be17c48e9ed344b3ca64"
        },
        "date": 1788516967240,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.149,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 29.357,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.774,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.819,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 144.802,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 282.355,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 67.815,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 88.543,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 69.037,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "f1d7f8f5efe0af6be6e2d23a65da0105a09da5ee",
          "message": "fix(http,graphql,storage): synchronize curl_global_init against concurrent actors (#156) (#164)\n\ncurl_global_init (libcurl) is documented by libcurl itself as NOT safe\nto call concurrently from multiple threads. Curry actors are real OS\nthreads with no global interpreter lock, so three separate call sites\nwere exposed:\n\n- modules/http/http.c's do_request had a racy hand-rolled\n  \"static int curl_inited = 0; if (!curl_inited) { curl_global_init(...);\n  curl_inited = 1; }\" -- an unsynchronized read-then-write, the classic\n  double-checked-init race: two actors both making their first\n  http-request concurrently could both observe curl_inited == 0 and\n  both call curl_global_init at the same time.\n- modules/graphql/graphql.c's fn_graphql_client and\n  modules/storage/storage.c's fn_swift_client/fn_azure_client had NO\n  guard at all -- curl_global_init was called unconditionally on every\n  single client construction, so two actors constructing their first\n  client (any combination of GraphQL/Swift/Azure) concurrently raced\n  on it directly, every time, not just on a rare first-call window.\n\nFixed with a pthread_once_t per module (one shared between\nfn_swift_client/fn_azure_client in storage.c, since both call into the\nsame underlying libcurl global state): pthread_once guarantees the\nwrapped call runs exactly once, thread-safely, regardless of how many\nthreads race to call it at the same time -- replacing http.c's racy\nhand-rolled check and adding real synchronization where graphql.c/\nstorage.c had none.\n\nConsidered and rejected moving curl_global_init into each module's\ncurry_module_init instead (the issue's other suggested option): that\nwould only move the race to concurrent first-import rather than close\nit, since curry_module_init is not actually guaranteed single-threaded\nin the presence of concurrent first-imports (a separate, already-fixed\nconcern, #149) -- and would still need its own synchronization to\nactually be race-free, at which point it's no simpler than fixing the\ncall sites directly. Also considered a single process-wide guard\nshared across all three modules (closing a narrower residual risk:\nliterally simultaneous first-use across TWO DIFFERENT modules, e.g. an\nhttp-request and a graphql-client construction landing in the exact\nsame instant on two threads) but rejected it: http/graphql/storage are\nthree separate, independently-optional .so module targets, and the\nonly place genuinely shared across all of them at the process level is\nthe core executable -- which does not currently link libcurl at all\n(only these three optional modules do), so exporting a shared guard\nfrom there would add libcurl as a new mandatory core dependency,\nreal unwanted scope creep for this fix. Each module's own guard closes\nthe actually-described race (concurrent first-use within one module,\nthe issue's own repro scenario); the narrower cross-module residual is\na much smaller, lower-probability risk than what existed before this\nfix (unconditional, always-racing calls in graphql.c/storage.c).\n\nAdded a regression test (tests/curl_global_init_race_tests.scm,\nregistered under the same BUILD_MODULE_HTTP/GRAPHQL/STORAGE guard s3's\nown test already uses): concurrent graphql-client/swift-client/\nazure-client construction across 8 actors (no network access needed,\nsince the race is in the constructor itself) plus concurrent\nhttp-request calls across 8 actors (skips cleanly if there's no\nnetwork access, matching network_tests.scm's own established\nconvention for network-dependent tests).\n\n118/118 ctest suites pass (fresh --clear-cache run, new suite\nincluded).\n\n\nClaude-Session: https://claude.ai/code/session_01BMiu9qzTUm6gzKJA2zkrQC\n\nCo-authored-by: Claude Sonnet 5 <noreply@anthropic.com>",
          "timestamp": "2026-09-04T20:38:21+10:00",
          "tree_id": "4b961bf8ab24dd2bae0969eb101384435ec829be",
          "url": "https://github.com/deconstructo/curry/commit/f1d7f8f5efe0af6be6e2d23a65da0105a09da5ee"
        },
        "date": 1788518339442,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.465,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 26.353,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.223,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.605,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 153.049,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 294.394,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.557,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 94.461,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 74.321,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "127fecba3a7b730fb2eb7ef121c358660fa1be00",
          "message": "fix(network,crypto,storage): reject non-bytevector arguments across the same unchecked-cast bug class (#161) (#168)\n\nFound during independent security review of #158's fix (which closed\nthe identical hazard for the socket HANDLE argument of these same two\nnetwork functions): fn_socket_send (modules/network/srfi106.c) and\nfn_udp_send (modules/network/network.c) both take a `data` argument\npassed straight to curry_bytevector_length/curry_bytevector_data/\ncurry_bytevector_ref with no curry_is_bytevector check at all. Those\nfunctions (src/api.c) do an unchecked as_bytes() cast assuming their\nargument already IS a bytevector -- confirmed reproducible SIGSEGV via\n(socket-send some-socket 42) and (udp-send some-socket 42 host port).\n\nA broader grep (recommended by the issue itself) across every C module\nfor the same \"unchecked curry_bytevector_length/data/ref on a\nuser-supplied argument\" pattern found four more genuinely vulnerable\ncall sites, all fixed here since they're the identical one-line fix:\n\n- modules/crypto/crypto.c: fn_base64_encode, fn_md5, hash_via_evp\n  (shared by fn_sha1/fn_sha256), and fn_hmac_sha256 (two arguments) --\n  none checked their bytevector argument(s) at all.\n- modules/storage/storage.c: fn_swift_put and fn_azure_put's `data`\n  argument (av[3]) -- same gap.\n\nmodules/rpi/rpi.c and modules/http/http.c's resolve_body already had\nthe correct curry_is_bytevector check in every relevant place -- not\ntouched.\n\nFixing crypto.c's functions surfaced that the old unchecked behavior\nwasn't merely a crash risk: passing a raw STRING (rather than the\ndocumented bytevector -- see docs/reference/module-crypto.md) never\nraised an error under the old code, it silently computed a WRONG hash,\nby misinterpreting a String object's own header layout (which happens\nto share the same Hdr+len prefix as Bytevector, but diverges after\nthat) as if it were a Bytevector's. Confirmed concretely: (md5-hex \"hi\")\nreturned a bogus, non-MD5 33-hex-character result under the pre-fix\ncode, not MD5(\"hi\")'s real, independently-verified value\n(49f68a5c8493ec2c0bf489821c21fc3b). tests/akkadian_tests.scm's own\nAkkadian-alias regression tests for md5-hex/sha1-hex/sha256-hex had\nbeen passing a raw string on both sides of each comparison for exactly\nthis reason -- both sides shared the identical wrong interpretation,\nso the test never noticed. Fixed those six checks to pass\n(string->utf8 \"hi\") instead, matching the base64-encode check\nimmediately above them (which was already doing this correctly).\n\nAdded a dedicated tests/crypto_tests.scm (no such file existed before\n-- (curry crypto) was previously exercised only incidentally via the\nAkkadian alias tests): known-answer tests for base64-encode/md5-hex/\nsha1-hex/sha256-hex/hmac-sha256 (values cross-checked against both\n`shasum` and `openssl dgst`, not just curry's own output), plus\nregression coverage confirming every fixed function now rejects a\nnon-bytevector argument cleanly. Also extended tests/network_tests.scm\nwith coverage for socket-send/udp-send's data argument specifically.\n\n119/119 ctest suites pass (fresh --clear-cache run, two new suites\nincluded: crypto, plus the extended network suite).",
          "timestamp": "2026-09-05T00:33:26+10:00",
          "tree_id": "badc09c1c36f3750ac0c5b66cf6b3813c4ce3fc5",
          "url": "https://github.com/deconstructo/curry/commit/127fecba3a7b730fb2eb7ef121c358660fa1be00"
        },
        "date": 1788532445234,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.025,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.844,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.202,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.728,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 153.197,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 284.171,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 66.012,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 94.252,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 74.562,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "650becb00edd62c064262844ac70da6b72ce54c8",
          "message": "fix(builtins): bytevector-length had no type check at all (#166) (#171)\n\nprim_bytes_length (src/builtins.c) cast its argument straight to a\nBytevector* via as_bytes() and dereferenced ->len, with no\nvis_bytes() check at all -- unlike every sibling bytevector\nprimitive (bytevector-u8-ref, bytevector-copy, bytevector-append,\netc.), which all correctly check first. This is R7RS core, always\npresent in GLOBAL_ENV, reachable with no import required.\n\nWorse than the #158/#161 pattern of misinterpreting one heap object\nas another: the argument can be an immediate value (fixnum/boolean/\nchar), whose raw tagged bit pattern gets dereferenced directly as a\npointer. Confirmed reproducible SIGSEGV via (bytevector-length 42)\nand (bytevector-length #t). A string argument didn't crash but\nreturned a wrong value (same \"silently wrong\" flavor as #161's\npre-fix crypto bug), since String's header happens to share enough\nof Bytevector's layout to read something.\n\nFixed by adding the same vis_bytes() check + scm_raise_code\n(EC_WRONG_TYPE_ARGUMENT, ...) every sibling primitive already uses.\nAdded regression coverage to tests/r7rs_tests.scm's existing\nbytevector section (fixnum, boolean, and string arguments all now\nraise cleanly instead of crashing or returning garbage).\n\n119/119 ctest suites pass (fresh --clear-cache run).",
          "timestamp": "2026-09-05T00:49:24+10:00",
          "tree_id": "6343e175baaaec9e3e83fc6ea51af08d8c809c24",
          "url": "https://github.com/deconstructo/curry/commit/650becb00edd62c064262844ac70da6b72ce54c8"
        },
        "date": 1788533412290,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 18.379,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 25.883,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 5.078,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 30.771,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 143.779,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 285.995,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.917,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 90.801,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 68.812,
            "unit": "ms"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "metanoia@gmail.com",
            "name": "deconstructo",
            "username": "deconstructo"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "c9dde8d2811e6ac4cc0374401da3d94699377e87",
          "message": "fix(builtins): add missing type checks to string-length, error-object-*, close-port, and hash-table-* (#170) (#174)\n\nFound during independent security review of #166's fix (bytevector-length\nhad no type check at all). Reviewed every prim_* in src/builtins.c for\nthe same shape of bug -- an as_TYPE(av[i]) cast with no preceding\nvis_TYPE(av[i]) check -- and found four more groups, all reachable from\nplain, no-import-needed R7RS-core code, all confirmed reproducible\nSIGSEGV pre-fix:\n\n- string-length (prim_string_length): as_str() with no vis_string()\n  check, unlike every sibling string primitive two lines below it.\n- error-object-message/error-message, error-object-irritants,\n  error-object-code (prim_error_message/prim_error_irritants/\n  prim_error_code): as_err() with no vis_error() check, unlike\n  error-object?/error-to-string right next to them.\n- close-port/close-input-port/close-output-port (prim_close_port,\n  shared by all three DEF names): port_close() called unconditionally,\n  and port_close() itself (src/port.c) also did as_port() with no\n  vis_port() guard at either layer.\n- The entire hash-table-* family: hash-table-set!/-ref/-delete!/\n  -exists?/-keys/-values/->alist/-size, all routed through\n  hash_set/hash_ref/hash_delete/hash_has/hash_keys/hash_values/\n  hash_to_alist/hash_size in src/set.c, none of which had ANY type\n  check on their table argument -- the widest-reaching instance, 8\n  always-bound core entry points. Fixed at the builtin call sites in\n  builtins.c (prim_hash_*) rather than in set.c's internal helpers,\n  matching where #166 fixed bytevector-length.\n\nEvery fix follows the exact vis_TYPE() + scm_raise_code\n(EC_WRONG_TYPE_ARGUMENT, ...) idiom every other checked primitive in\nthe same files already uses. No behavior change on the correct-argument\npath.\n\nAdded regression coverage to tests/r7rs_tests.scm alongside each\nexisting section for these functions: string-length, error-object-*,\nclose-port, and all 8 hash-table-* entries now confirmed to raise\ncleanly instead of crashing.\n\n483/483 r7rs_tests.scm assertions pass; 119/119 ctest suites pass\n(fresh --clear-cache run).",
          "timestamp": "2026-09-05T01:09:21+10:00",
          "tree_id": "4b324d346873c37cceb73598794c93e6db83ccba",
          "url": "https://github.com/deconstructo/curry/commit/c9dde8d2811e6ac4cc0374401da3d94699377e87"
        },
        "date": 1788534600715,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "fib(25)/vm",
            "value": 17.427,
            "unit": "ms"
          },
          {
            "name": "fib(22)/tw",
            "value": 30.033,
            "unit": "ms"
          },
          {
            "name": "tak(18,12,6)/vm",
            "value": 4.825,
            "unit": "ms"
          },
          {
            "name": "tak(16,10,4)/tw",
            "value": 34.868,
            "unit": "ms"
          },
          {
            "name": "count-down(3M)/vm",
            "value": 132.804,
            "unit": "ms"
          },
          {
            "name": "flonum-loop(1M)",
            "value": 278.941,
            "unit": "ms"
          },
          {
            "name": "cont-capture(200k)",
            "value": 65.566,
            "unit": "ms"
          },
          {
            "name": "alloc-churn(1M)",
            "value": 87.779,
            "unit": "ms"
          },
          {
            "name": "list-build-walk(500k)",
            "value": 66.584,
            "unit": "ms"
          }
        ]
      }
    ]
  }
}