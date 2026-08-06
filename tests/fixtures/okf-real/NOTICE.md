# Provenance

`acme_retail/` is vendored verbatim (minus `viz.html`, which isn't
relevant to this module and isn't markdown) from the real `acme_retail`
sample bundle in the [OKF reference implementation](https://github.com/GoogleCloudPlatform/knowledge-catalog/tree/main/okf),
commit `930b65fc3f5619d5d0591f88c72ebae8b848d60d`
(`okf/bundles/acme_retail`).

It exists as ground truth: `tests/fixtures/okf/` is a synthetic bundle
written to match this project's own reading of the OKF spec, so it can
share the same blind spots as the code it tests. This is a real,
independently-authored bundle exercising the spec's actual field shapes
(flow-map `sources`/`verified`/`parameters` entries, `Attested
Computation` contracts, cross-directory links) that the code under test
was never shaped around.

Licensed under Apache License 2.0, per the source repository's
`okf/LICENSE.md`.
