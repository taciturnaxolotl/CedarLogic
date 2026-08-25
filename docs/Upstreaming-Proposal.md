# Bringing CedarLogic Under Cedarville Ownership

**A proposal for Information Technology Services**

| | |
| --- | --- |
| **Prepared for** | Cedarville University IT |
| **Subject** | Code-signing certificates, developer accounts, and repository ownership for CedarLogic |
| **Status** | Proposal — awaiting IT review |
| **Faculty sponsor** | School of Engineering and Computer Science (Computer Architecture) |
| **Recurring cost requested** | **$219.88/year** (potentially $119.88 with an Apple fee waiver) |
| **One-time IT effort** | Roughly 6–10 staff-hours, spread over about four weeks |

---

## 1. The ask in one paragraph

CedarLogic is the digital logic simulator Cedarville professors wrote for Computer
Architecture, and it is still the tool students use to build a working Mano machine.
It has been actively rebuilt over the past two years and now ships installers for
Windows, macOS, and Linux, plus a WebAssembly build. Everything about that release
pipeline currently runs on **personal accounts belonging to a student maintainer** —
the code repository, the software's digital signature, the auto-update feed that every
installed copy phones home to, and the signing keys that authorize those updates. We
are asking IT to bring that infrastructure onto university-owned accounts: an Apple
Developer Program membership, a Windows code-signing identity, a Cedarville GitHub
organization, and a `cedarville.edu` subdomain for the update feed. The total is about
$220 a year. The point is not the money; it is that the university should own the
identity it is publishing software under, and should not depend on a graduating
student's personal accounts to keep a course-critical tool working.

---

## 2. Why this is worth IT's time

**CedarLogic is used, and not only here.** It was published on SourceForge in 2006 and
drew over 20,000 downloads in 2019 alone. The Cedarville name is on it — the app
literally reports its publisher as "Cedarville University," the macOS bundle
identifier is `edu.cedarville.cedarlogic`, and the WebAssembly build is published to
npm under the `@cedarville` scope. The university's name is already attached to this
software; what is missing is the university's control over it.

**Three specific things are wrong today.** Each is described in detail in §3, but in
short:

1. The **Windows installer is not digitally signed at all.** Every student who
   downloads it gets a full-screen "Windows protected your PC" warning and has to
   click through "More info → Run anyway" to install a tool assigned for a class.
2. The **Windows auto-updater does not verify signatures.** The app checks for updates
   in the background and installs what the feed hands it, with no cryptographic check
   on the file. The feed is hosted on a personal GitHub Pages site.
3. The **macOS build is signed by a private LLC**, not by Cedarville. Students see a
   third-party company's name where the university's should be, and nobody at
   Cedarville can revoke, re-sign, or ship an update if that account goes away.

**The window to fix it cleanly is now.** The current maintainer is available and
willing to hand over the infrastructure and rewire the automation. Doing this after
they leave means recovering accounts nobody has credentials for, and — in the worst
case — abandoning the update channel and asking every existing installation to
uninstall and reinstall from scratch.

---

## 3. Where things stand today

### 3.1 How a release is built

Releases are fully automated. A maintainer pushes a version tag; GitHub Actions builds
all four artifacts, signs what it can, publishes a GitHub release, regenerates the
auto-update feed, and pushes the WebAssembly build to npm. Nothing is hand-built or
hand-uploaded. **This proposal does not ask to change any of that** — the pipeline is
sound. It asks to change *whose accounts it runs under*.

### 3.2 Signing status by platform

| Platform | Artifact | Signed? | Signed by whom | What a user sees |
| --- | --- | --- | --- | --- |
| Windows | `CedarLogic-x.y.z-win32.exe` (NSIS installer) | **No** | — | SmartScreen: "Windows protected your PC. Unknown publisher." |
| macOS | `CedarLogic-x.y.z-Darwin.dmg` | Yes — signed, hardened runtime, notarized, stapled | **Single Feather LLC** (a private company, not Cedarville) | Installs cleanly; publisher shows as Single Feather LLC |
| Linux | `.tar.gz` | N/A (SHA-256 checksums published) | — | Normal for the platform |
| Web / npm | `@cedarville/cedarlogic-engine` | Published via npm trusted publishing (OIDC) | GitHub Actions, no stored token | Fine as-is |

### 3.3 The auto-update channel

Both the Windows and macOS builds check for updates automatically and can install them
without the user visiting a website. That is a convenience for students and a
significant trust boundary for IT — it is a channel that can put executable code onto a
university machine.

| | macOS (Sparkle) | Windows (WinSparkle) |
| --- | --- | --- |
| Update feed URL | `taciturnaxolotl.github.io/CedarLogic/appcast.xml` | same |
| Feed hosted by | A personal GitHub account | A personal GitHub account |
| Signature on the update | **Yes** — Ed25519, verified against a key baked into the app | **No key is configured at all** |
| Signing key custody | A secret in a personal GitHub repository | — |

The macOS side is done properly: each release is signed with an Ed25519 key, and the
app refuses any update that does not verify against the public key compiled into it.
**The Windows side has no equivalent.** The application never sets an update-signing
key and embeds no public key resource, so the Windows updater's only protection is that
the download arrives over HTTPS from GitHub. Anyone who controls the personal GitHub
account hosting that feed controls what every Windows installation of CedarLogic
downloads and executes next.

That is not a hypothetical about a bad actor; it is an ordinary account-lifecycle
problem. A forgotten password, a lapsed personal account, or an account recycled by
GitHub is enough to break or hijack the channel, and nobody at Cedarville has standing
to intervene.

### 3.4 Credentials currently held outside the university

These are the secrets the release pipeline uses today. All of them live in a
personal GitHub repository, and all of them would move to university-owned accounts
under this proposal.

| Secret | What it authorizes | Held by |
| --- | --- | --- |
| `APPLE_CERTIFICATE_BASE64` / `_PASSWORD` | Signing macOS software as the certificate holder | Single Feather LLC |
| `APPLE_ID` / `APPLE_ID_PASSWORD` / `APPLE_TEAM_ID` | Submitting builds to Apple for notarization | Single Feather LLC |
| `SPARKLE_ED_PRIVATE_KEY` | **Authorizing any macOS auto-update** — the root of trust for the update channel | Personal repo secret |
| `RELEASE_TOKEN` | An admin token that can push to the protected `master` branch | Personal account |

The `SPARKLE_ED_PRIVATE_KEY` deserves emphasis: possession of that key is sufficient to
push an update to every Mac running CedarLogic. It should be held in an institutional
vault, not a personal repository.

---

## 4. What we are asking IT to provide

Six items. Two cost money; four are administrative.

### 4.1 A role account — the prerequisite for everything else

Every account below must be registered to a **departmental role mailbox**
(e.g. `cedarlogic@cedarville.edu`), never to an individual's account. This is the
single most important item in the proposal: it is what makes every other account
survivable when people change. The mailbox should be owned by IT or the department,
with the faculty sponsor and the current maintainer as members.

> **Cost:** none. **Effort:** one mailbox request.

### 4.2 Apple Developer Program — Organization enrollment

Required to sign and notarize the macOS build under Cedarville's name. Apple issues
"Developer ID" certificates only to enrolled members, and macOS refuses to run
downloaded software that is not signed and notarized by one.

- **Enrollment type:** Organization (*not* Individual — an individual enrollment
  publishes under a person's legal name and cannot be transferred)
- **Requires:** the university's D-U-N-S number, legal entity name, a website, and a
  person with legal signing authority to accept the agreement
- **Cost:** $99/year
- **Possible waiver:** Apple waives the annual fee for accredited educational
  institutions. The published waiver terms are written around institutions distributing
  free apps *through the App Store*, and CedarLogic is distributed directly rather than
  through the App Store, so the waiver may not apply. It costs nothing to request it
  during enrollment — **but the proposal is budgeted assuming we pay the $99.**
- **Lead time:** typically a few business days; longer if Apple asks for documentation

What IT hands us afterward: nothing directly. IT holds the account, and adds the
maintainer to the team with the "Developer" role so that CI can request a signing
certificate. Certificate private keys can be generated and held by IT if preferred.

### 4.3 A Windows code-signing identity

This is what removes the SmartScreen warning and lets us turn on signature
verification for Windows updates.

**An important constraint IT should know up front:** since June 1, 2023, industry rules
(the CA/Browser Forum baseline requirements) prohibit code-signing private keys from
existing as ordinary files. The key must live on a certified hardware token (FIPS 140-2
Level 2 or equivalent) or in a cloud HSM. **No vendor can email us a `.pfx` file
anymore.** Any option below therefore involves either a physical USB token that has to
be plugged into a machine — which does not work with automated cloud builds — or a
cloud signing service. We strongly recommend a cloud service.

A second useful fact: **an Extended Validation (EV) certificate is no longer worth the
premium for us.** EV certificates historically bypassed SmartScreen warnings
immediately; since March 2024 Microsoft treats EV and OV certificates the same for
SmartScreen reputation, which now builds from download volume over time. EV remains
necessary only for kernel-mode drivers, which CedarLogic is not.

#### Option A — Azure Artifact Signing *(recommended)*

Microsoft's own managed signing service, formerly called Trusted Signing. Keys are
generated and held in Microsoft's HSMs and never exist outside them; our build pipeline
authenticates to Azure and asks the service to sign, rather than ever holding a key.

- **Cost:** $9.99/month Basic tier (5,000 signatures/month; we use roughly 1 per
  release), $119.88/year
- **Requires:** an Azure subscription and a one-time organization identity validation
  (legal name, address, website, business email). Available to US, Canadian, EU, and UK
  organizations. The earlier "three years of verifiable legal existence" rule was
  dropped in 2026, and it would not have been an obstacle for the university regardless.
- **Why we recommend it:** cheapest, no hardware to ship or store, integrates with
  GitHub Actions using short-lived federated credentials so **no exportable secret ever
  leaves Azure**, and it is Microsoft's first-party path. If Cedarville already has an
  Azure tenant, this is a line item on an existing subscription rather than a new vendor.
- **Lead time:** identity validation typically 1–7 business days

#### Option B — A commercial OV certificate with cloud signing

DigiCert (KeyLocker), SSL.com (eSigner), Sectigo, and others sell an organization-
validated certificate paired with a cloud HSM.

- **Indicative cost:** roughly $250–$600/year depending on vendor and term — get a
  quote; several vendors offer education pricing
- **Requires:** organization validation (legal existence, address, phone verification
  against a public directory) — comparable paperwork to Option A
- **When to prefer it:** if the university already has a certificate relationship with
  one of these CAs and prefers to consolidate vendors, or if the Azure path is blocked
  for procurement reasons
- **Note:** certificates issued from March 1, 2026 are capped at 460 days validity, so
  this becomes a recurring renewal task on someone's calendar

#### Option C — SignPath Foundation (free, for open-source projects)

SignPath Foundation provides free HSM-backed code signing to qualifying open-source
projects, which CedarLogic is (MIT-licensed, public repository).

- **Cost:** free
- **Trade-off:** the signing identity is the Foundation's, not Cedarville's, so the
  publisher shown to students would read as the Foundation rather than the university.
  That undercuts the main point of this proposal. **Verify the displayed publisher name
  during application before relying on this.**
- **When to prefer it:** as a fallback if no budget is available at all. It solves the
  security problem while leaving the identity problem unsolved.

### 4.4 A Cedarville GitHub organization

The repository currently lives at `github.com/taciturnaxolotl/CedarLogic`, a personal
account. We ask IT to create (or nominate an existing) Cedarville GitHub organization
and accept a transfer of the repository into it.

- **Cost:** free for public repositories on GitHub's Free plan. GitHub Team ($4/user/
  month) buys required reviewers on private repos and finer-grained controls; we do not
  believe it is necessary here, since the repository is and should remain public.
- **What transfers with the repo:** all history, issues, pull requests, tags, releases,
  and — importantly — **GitHub automatically redirects the old URL**, so existing
  clones, links in course materials, and bookmarks keep working.
- **What does not transfer:** repository secrets and the GitHub Pages site. Those are
  re-created in the new organization, which is exactly what we want.
- **Access model we propose:** the faculty sponsor and IT hold Owner; the maintainer
  holds Admin on the repository; release-signing secrets are org-level and scoped to
  the one repository; branch protection on `master` stays as it is (signed commits,
  passing CI required).

### 4.5 A DNS record for the update feed

The update feed must move off the personal GitHub Pages domain and onto university DNS.
We propose a CNAME:

```
cedarlogic.cedarville.edu.   CNAME   <cedarville-org>.github.io.
```

This is the single highest-value item in the entire proposal relative to its cost. It
means the university — not any individual — controls the address every installed copy
of CedarLogic asks for updates, permanently. Even if the GitHub organization changed
hands later, the DNS record is the authority.

> **Cost:** none. **Effort:** one DNS record, plus a GitHub Pages custom-domain setting.

### 4.6 Custody of the update-signing keys

Two Ed25519 keys authorize auto-updates (one for macOS, one for Windows once we enable
verification). We ask that:

- both be generated fresh under the new organization,
- the private keys be stored in the university's password manager or secret vault, with
  the working copies as organization-level GitHub Actions secrets scoped to this
  repository,
- the old macOS key be retired after one transitional release (see §7).

---

## 5. Cost summary

| Item | Recommended path | Annual cost |
| --- | --- | --- |
| Apple Developer Program (Organization) | Required for macOS | $99.00 |
| Azure Artifact Signing, Basic tier | Option A above | $119.88 |
| GitHub organization | Free plan, public repository | $0.00 |
| `cedarlogic.cedarville.edu` DNS record | Existing university DNS | $0.00 |
| Role mailbox | Existing university mail | $0.00 |
| **Total** | | **$219.88/year** |

**If Apple grants an educational fee waiver:** $119.88/year.
**If Option C (SignPath) is used instead of Azure:** $99.00/year, at the cost of the
publisher identity.
**Absolute floor, if no budget is available at all:** $0, using SignPath for Windows and
leaving macOS signed by the current third party — this fixes the security gap but not
the ownership gap, and we do not recommend it.

For context, this is materially less than one seat of most commercial engineering
software, for a tool an entire course depends on.

---

## 6. What IT is actually agreeing to (security review)

We expect IT's main question to be "what are we taking on?" Concretely:

**Private keys.** Under the recommended path, no code-signing private key is ever
downloadable, by us or by anyone. Azure Artifact Signing generates and holds the key in
a Microsoft HSM; the build pipeline authenticates with a short-lived federated token
and submits a hash to be signed. The Apple certificate is the one exception — it is a
file — and IT may generate and hold it directly, or delegate to the maintainer, at IT's
preference.

**Who can publish signed software.** Only a version tag pushed to the `master` branch
of the university-owned repository triggers a signed release. `master` requires signed
commits and passing CI, and only repository admins can push to it. Signing therefore
requires both repository admin access *and* the org's signing credentials.

**Audit trail.** Every signature is logged by Azure. Every release is a public GitHub
release with published SHA-256 checksums. Every code change is a reviewed pull request.
There is no path by which signed software is produced without a public record.

**Offboarding.** Because every account is registered to a role mailbox and every
credential lives at the organization level, removing a departing maintainer is a matter
of removing one GitHub team membership. No credentials are re-issued and nothing breaks.

**Revocation.** If a key is ever suspected compromised, IT can revoke the certificate
with the issuing authority and rotate the update-signing keys; the university controls
the DNS name the feed lives on, so it can also point the feed elsewhere immediately.

**What IT is not taking on.** IT is not being asked to build, test, review, or support
the software. Development and maintenance stay with the faculty sponsor and student
maintainers. The ask is custody of identity and credentials.

---

## 7. What changes on our side

For completeness, the engineering work this unlocks — all of it ours to do, at no cost
to IT:

1. **Sign the Windows installer.** Add a signing step to the release pipeline after the
   installer is packaged. Removes the SmartScreen "unknown publisher" warning; the
   reputation that suppresses the remaining warning entirely accrues over the first
   several releases.
2. **Turn on Windows update verification.** Generate an Ed25519 key pair, embed the
   public key in the application, and sign each Windows release. This closes the gap
   described in §3.3 — after this change the Windows updater refuses any update it
   cannot verify.
3. **Re-sign macOS under the university's Developer ID.** Swap the certificate and
   notarization credentials. Because the update is verified by the *Sparkle* key rather
   than the Apple certificate, this is a clean change for existing installations.
4. **Move the update feed to `cedarlogic.cedarville.edu`.** This requires one
   transitional release: existing installations still point at the old feed, so the old
   feed must keep serving — and must redirect — until installations have updated past
   the transition. We propose keeping the old URL alive and pointing at the new location
   for **at least one full academic year**.
5. **Rotate the update-signing keys.** The macOS key is rotated with a release signed by
   both the old and the new key, so that older installations can verify the update that
   teaches them the new key. This is a standard Sparkle procedure and must be done in the
   correct order — a mistake here strands existing installations with no update path.
6. **Replace the admin token** used by the release pipeline with an organization-owned
   equivalent or a GitHub App.

---

## 8. Proposed sequence

| Phase | What happens | Owner | Elapsed |
| --- | --- | --- | --- |
| 0 | IT reviews this proposal; decisions in §9 settled | IT + faculty | Week 0 |
| 1 | Role mailbox created; GitHub organization created | IT | Week 1 |
| 2 | Apple enrollment submitted; Azure identity validation submitted (both have external review queues — start them in parallel and early) | IT | Week 1 |
| 3 | Repository transferred; DNS record added; GitHub Pages custom domain configured | IT + maintainer | Week 2 |
| 4 | New signing keys generated and stored; org secrets configured | IT + maintainer | Week 2–3 |
| 5 | Pipeline rewired; a release candidate built and verified on clean Windows and macOS machines | Maintainer | Week 3–4 |
| 6 | First fully university-signed release published | Maintainer | Week 4–5 |
| 7 | Old feed left in redirect for one academic year, then retired | Maintainer | +12 months |

The two external dependencies — Apple's enrollment review and Azure's identity
validation — are the only steps with unpredictable duration, which is why they are
started first. If this is approved before the semester, a signed release is realistic
within about a month.

---

## 9. Decisions we need from IT

1. **Windows signing:** Option A (Azure Artifact Signing, recommended), Option B
   (commercial OV certificate), or Option C (SignPath, free)?
2. **Azure:** does the university have an existing Azure tenant this can be attached to,
   or is a new subscription needed?
3. **GitHub organization:** does a Cedarville-owned organization already exist that this
   should live in, or should one be created?
4. **Apple certificate custody:** does IT want to generate and hold the Developer ID
   certificate, or delegate certificate generation to the maintainer under the
   university's team?
5. **Role mailbox name:** `cedarlogic@cedarville.edu`, or a naming convention IT
   prefers?
6. **Budget line:** which account absorbs the ~$220/year, and who is the renewal owner
   when the maintainer graduates?

Item 6 is the one most likely to be forgotten and most likely to hurt. An expired Apple
membership does not merely block new releases — it eventually invalidates the ability to
notarize, and an unrenewed signing certificate silently returns us to where we are today.

---

## 10. If we do nothing

- Students continue clicking through malware warnings to install required coursework
  software, which is poor practice to teach and a support burden every semester.
- The Windows update channel continues to accept unverified code, hosted on a personal
  account outside university control.
- The university's name continues to appear on software the university cannot sign,
  revoke, or update.
- When the current maintainer leaves, the update channel and release pipeline become
  unrecoverable without account access nobody has, and the practical remedy is asking
  every user to uninstall and reinstall from a new source.

None of these are urgent this week. All of them get harder, not easier, with time.

---

## Appendix A — Glossary

**Code signing** — attaching a cryptographic signature to a program that proves who
published it and that it has not been altered since. Operating systems warn about, or
refuse to run, unsigned software downloaded from the internet.

**Authenticode** — Microsoft's code-signing system for Windows.

**SmartScreen** — the Windows reputation service that shows "Windows protected your PC"
for software it does not recognize. A signature gives it a stable publisher identity to
build reputation against.

**Notarization** — Apple's automated malware scan. macOS refuses to open downloaded
software that has not been through it, in addition to requiring a signature.

**Developer ID** — the Apple certificate type used for software distributed outside the
Mac App Store. CedarLogic is distributed this way.

**Gatekeeper** — the macOS component that enforces the signing and notarization
requirements.

**HSM (Hardware Security Module)** — tamper-resistant hardware that holds a private key
and performs signatures without ever releasing the key. Required by industry rules for
code-signing keys since June 2023.

**OV / EV** — Organization Validated and Extended Validation, two levels of vetting for
a code-signing certificate. Since March 2024 they behave identically for SmartScreen
purposes.

**Sparkle / WinSparkle** — the open-source auto-update frameworks CedarLogic uses on
macOS and Windows respectively.

**Appcast** — the XML feed an installed application checks to discover new versions.

**D-U-N-S number** — a business identifier Apple requires for organization enrollment.
Universities have one.

## Appendix B — Reference links

- Apple Developer Program enrollment — <https://developer.apple.com/programs/enroll/>
- Apple Developer Program fee waiver — <https://developer.apple.com/support/fee-waiver>
- Azure Artifact Signing (formerly Trusted Signing) — <https://azure.microsoft.com/en-us/products/artifact-signing>
- Microsoft's code-signing options for Windows developers — <https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options>
- SignPath Foundation, free code signing for open source — <https://signpath.org/>
- Sparkle documentation — <https://sparkle-project.org/documentation/>
- WinSparkle — <https://winsparkle.org/>
- CedarLogic release process — [`docs/Releasing.md`](./Releasing.md)
- CedarLogic project history and original authors — [`docs/History.md`](./History.md)

---

*Pricing and program requirements stated here were verified in August 2026 and should be
confirmed at the time of purchase.*
