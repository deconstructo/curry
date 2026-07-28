window.BENCHMARK_DATA = {
  "lastUpdate": 1785230553729,
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
      }
    ]
  }
}