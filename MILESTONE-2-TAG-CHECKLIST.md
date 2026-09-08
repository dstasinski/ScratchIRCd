# Milestone 2 Tag Checklist

Use this checklist after the Milestone 2 release-gate runner has completed successfully.

## 1. Identify the tested commit

The tested commit is the commit recorded by the release-gate evidence, not necessarily the latest commit on `Genesis` after documentation updates.

```sh
cd ~/Projects/ScratchIRCd
cat release-evidence/milestone-2/environment.txt
grep '^commit=' release-evidence/milestone-2/environment.txt
cat release-evidence/milestone-2/summary.log
```

Confirm that `summary.log` shows exit `0` for every required gate that was run.

## 2. Choose which commit to tag

### Option A: Tag the exact commit already tested

Use this when no code or documentation changes after the successful gate need to be included in the tag.

```sh
TESTED_COMMIT=$(grep '^commit=' release-evidence/milestone-2/environment.txt | cut -d= -f2-)
git fetch origin Genesis
git tag -a milestone-2 "$TESTED_COMMIT" -F MILESTONE-2-RELEASE-NOTES.md
git push origin milestone-2
```

### Option B: Include later documentation commits in the tag

Use this if the tag should include the final release notes, tag checklist, README, or guide updates made after the previous gate run.

```sh
git checkout Genesis
git pull --ff-only origin Genesis
rm -rf build-m2-gcc build-m2-clang build-m2-sanitize release-evidence/milestone-2
./tools/milestone2-release-gate.sh

TESTED_COMMIT=$(grep '^commit=' release-evidence/milestone-2/environment.txt | cut -d= -f2-)
git tag -a milestone-2 "$TESTED_COMMIT" -F MILESTONE-2-RELEASE-NOTES.md
git push origin milestone-2
```

## 3. Preserve release evidence

Keep the generated evidence directory outside the working tree or archive it with release records:

```sh
release-evidence/milestone-2/
```

At minimum, preserve:

```text
environment.txt
summary.log
full-gcc-build.log
full-gcc-ctest.log
full-clang-build.log, if Clang was available
full-clang-ctest.log, if Clang was available
sanitizer-build.log
sanitizer-ctest.log
soak-smoke.log
soak-release-gate.log
```

## 4. Optional longer soak

The built-in gate includes a smoke soak and release-gate coverage check. For extra confidence before publishing the tag, run a longer soak against the tested build:

```sh
python3 tools/run_soak.py ./build-m2-gcc/scratchircd \
  --duration-hours 12 \
  --clients 12 \
  --release-candidate \
  --report "soak-$(git rev-parse --short HEAD).json"
```

Do not tag a different commit after a longer soak unless that exact commit is the one that was tested.
