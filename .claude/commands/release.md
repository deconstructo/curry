# /release — End-to-end curry release pipeline

Runs the full release pipeline for the version passed as the argument (e.g. `/release 1.2.2`).

**Every checkpoint ends with an explicit confirmation before proceeding.** Never skip a checkpoint or batch steps across checkpoint boundaries. Never push without explicit user confirmation.

---

## Checkpoint 0: Preflight

Run these checks. Stop and report all failures before asking for confirmation.

```bash
git status --porcelain
```
Abort if anything is printed (dirty working tree).

```bash
git log --oneline origin/main..HEAD
```
Abort if unpushed commits exist that aren't part of the release being prepared. (A pre-existing bump commit from a previous partial attempt is fine — note it.)

Extract OLD_VERSION from `src/version.h`:
```bash
grep -o '"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*"' src/version.h | tr -d '"'
```

Verify that `CHANGELOG.md` already contains a section header for NEW_VERSION. Look for `### NEW_VERSION` near the top. If it does not exist, **abort with this message**:

> No CHANGELOG entry found for vNEW. Write the changelog section first, then re-run /release NEW.

Read `Formula/curry.rb` and note the current URL tag, sha256, and version field.

Show a preflight summary table:

| Field | Current | After release |
|---|---|---|
| src/version.h | OLD | NEW |
| Formula URL tag | old_tag | vNEW |
| Formula version field | old_formula_ver | NEW |
| Formula sha256 | current_hash | (computed after tag push) |
| Git tag | latest_tag | vNEW |
| Doc stamp pattern | *vOLD — DATE* | *vNEW — TODAY* |

**Ask for confirmation before proceeding.**

---

## Checkpoint 1: Version bump commit

Make all edits, then show a `git diff --stat`, then commit.

### 1a. src/version.h

Replace `CURRY_VERSION "OLD"` → `CURRY_VERSION "NEW"`.

### 1b. CHANGELOG.md

No edit needed (user already wrote the section). Verify the section is present, then leave it.

### 1c. Doc stamps — all docs/**/*.md files

Find all doc files containing the old stamp pattern `*vOLD`:
```bash
grep -rl "\*v$(grep -o '[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*' src/version.h)" docs/
```

For each file, replace:
- `*vOLD — YYYY-MM-DD*` → `*vNEW — TODAY*`

where TODAY is the current date in YYYY-MM-DD format.

### 1d. docs/roadmap.md

Make three targeted replacements:
1. `Curry is at vOLD.` → `Curry is at vNEW.`
2. `Updated PREV_DATE for vOLD` → `Updated TODAY for vNEW` (in the italic stamp line)
3. `## Where we are now (vOLD)` → `## Where we are now (vNEW)`

### 1e. Formula/curry.rb

Make exactly four changes:

1. In-comment git tag example: `git tag vOLD` → `git tag vNEW`
2. In-comment curl example URL: replace old tag with `vNEW`
3. `url` line: replace old tag reference with `vNEW`
4. `version` field: `version "OLD"` → `version "NEW"` (strip leading v)
5. Set sha256 to placeholder with trailing comment:
   ```
   sha256 "0000000000000000000000000000000000000000000000000000000000000000"  # update after tag
   ```

### 1f. Commit

```bash
git add src/version.h CHANGELOG.md Formula/curry.rb docs/
git commit -m "chore: bump version to NEW; update changelog and doc stamps"
```

Run `git show --stat HEAD` to confirm what was committed.

**Ask for confirmation before tagging.**

---

## Checkpoint 2: Tag and push

```bash
git tag -a vNEW -m "Release vNEW"
git push origin vNEW
```

After the push, print:
```
Tag vNEW pushed. GitHub will generate the tarball at:
https://github.com/deconstructo/curry/archive/refs/tags/vNEW.tar.gz
```

Wait 8 seconds, then fetch the SHA256 with up to 4 retries (10 s apart):

```bash
curl -sL --fail https://github.com/deconstructo/curry/archive/refs/tags/vNEW.tar.gz | shasum -a 256
```

If the curl returns a non-200 or empty output on all attempts, stop and tell the user to run `scripts/release-verify.sh` manually once the tarball is available.

Display the SHA256. Verify it is exactly 64 lowercase hex characters and is not all zeros.

**Ask for confirmation that the SHA256 looks valid before updating the formula.**

---

## Checkpoint 3: Formula SHA256 commit

Edit `Formula/curry.rb`:
- Replace the 64-zero placeholder sha256 with the computed hash
- Remove the trailing `# update after tag` comment from that line

```bash
git add Formula/curry.rb
git commit -m "chore(formula): update sha256 for vNEW tarball"
git push origin main
```

Run `git log --oneline -3` to confirm the push landed.

**Ask for confirmation before running the post-release verification.**

---

## Checkpoint 4: Post-release verification

```bash
bash scripts/release-verify.sh vNEW
```

Report the full output. If verification fails, display the error and suggest manual steps.

---

## Completion summary

Print:

```
Released vNEW
  bump commit : <sha>
  tag         : vNEW  →  pushed to origin
  sha256      : <hash>
  formula     : Formula/curry.rb updated and pushed
  brew verify : PASS / FAIL

Next: update docs/roadmap.md shipped-versions table if this was a minor release.
```
