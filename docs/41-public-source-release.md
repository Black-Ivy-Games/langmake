# Public source release

Langmake's original code and example content use the MIT license. People may use,
modify, redistribute and sell the engine and their games, including closed-source
versions, while retaining the applicable copyright and license notices. Dependencies
keep their own terms in `THIRD_PARTY_NOTICES.md`, `dependencies.lock.json` and
`licenses/`. No royalties or requirement to publish game source are imposed by
Langmake's MIT license.

## October 13, 2026 privacy review

The public-source review covered all 449 originally tracked files, all 491 unique
historical blobs reachable from local Git refs, commit metadata, dependency notices,
source/test configuration and the sole tracked image. It also inventoried ignored
local state and credential-named files. Secret-pattern matches were manually
classified; upstream copyright contact details and explicit test credentials are
retained. The tracked image is an original game icon, not a device capture.

Public documentation now omits the developer's account names, private network
addresses, SSH alias/configuration, Apple signing team, local workspace paths and
specific Mac host memory/processor details. Native device identifiers in tests are
explicit synthetic fixtures. Tool/OS versions and hardware model coverage remain
where they establish historical qualification; they do not identify individual
devices. Redacted historical records retain their original qualification limits.
This review does not claim new runtime, platform or gameplay qualification.

No actual password, passphrase, provider token or private key was identified in the
audited tracked source or reachable Git history. This is the result of a pattern
scan and manual review, not a guarantee that arbitrary future content is safe.

## October 14, 2026 history-preserving publication copy

The separate `dist/Langmake-public-history-2026-10-14` checkout preserves all six
original `main` commits, with their messages, author/committer dates, parent order
and file modes. Fourteen historical blob versions were redacted across fourteen
paths. Personal commit identities were replaced with
`Langmake contributors <contributors@langmake.invalid>`. The neutral `.invalid`
address is a reserved placeholder, not a contact address. Commit hashes changed;
historical engineering records may still cite internal pre-rewrite revision IDs.

The cleaned historical repository was audited across all 499 stored source blobs
and all six commit objects, including unreachable objects. All six rewritten
revisions were compared against their originals: the only source differences are
the approved privacy redactions and synthetic test identifiers. Their latest
source tree matches the already-sanitized October 13 release tree. `git fsck
--full` passes and no known private identity or unreviewed secret/address match
remains. A publication-guide commit records this handoff; it is separate from the
six historical development commits. See the [historical audit checkpoint](releases/public-history-review-20261014.json).
This does not claim new native/runtime or platform qualification.

The original development checkout, local target configuration and Gitea connection
remain unchanged. Only `refs/heads/main` is retained in the public copy; local
Codex capture/checkpoint refs and remote-tracking refs are excluded. No remote is
configured and nothing has been pushed. The matching history bundle is
`dist/Langmake-public-history-2026-10-14.bundle` and can be cloned into a new
working directory without copying the original checkout's `.git` directory.

To publish with history, use this cleaned copy and an empty GitHub repository:

```powershell
# From the development workspace root; use the cleaned copy below.
Set-Location dist/Langmake-public-history-2026-10-14
git remote add origin https://github.com/YOUR_GITHUB_USERNAME/langmake.git
git push -u origin main
```

Keep the cleaned publication lineage for future GitHub commits. Do not merge or
fetch the unfiltered Gitea history into it. Review any newly copied changes before
committing; commit using a public-safe identity. The local neutral identity is
configured only in the publication copy, not globally or in the original checkout.

## Snapshot upload boundary

The prepared `dist/Langmake-public-source-2026-10-13.zip` and matching extracted
directory contain the sanitized source snapshot and license inventory. They contain
no `.git` directory, commit authors, original history, installed dependencies,
compiled players, local games, raw receipts, caches, SSH/signing files, provider
settings or chat state. The release includes a `PUBLIC-SOURCE-MANIFEST.json` with
the file list and SHA-256 hashes. The source archive is built from an explicit file
list, not by recursively copying the development workspace.

Start a **new Git repository** from the extracted source directory when uploading
this snapshot. Do not copy the development checkout's `.git` directory or push its
old branches/tags: its historical blobs still contain the removed private details.
The original checkout and working local target settings are preserved so development
can continue. Existing ignored binary distributions and raw evidence are outside
this source audit and must not be included in the upload.

Before future releases, review newly tracked files, committed history and staged
changes for credentials, private addresses, device/account identifiers and local
paths. Keep `.langmake/`, `.cache/`, `.deps/`, `build/`, `dist/`, `evidence/`, personal
`games/`, signing material and provider state excluded. Never force-add local
credentials merely because a packaging or deployment tool needs them. Regenerate
portable documentation from canonical sources when preparing a new binary release.
