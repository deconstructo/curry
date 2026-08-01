window.BENCHMARK_DATA = {
  "lastUpdate": 1785621976461,
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
      }
    ]
  }
}