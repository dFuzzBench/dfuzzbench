# Source images of the knowledge-cutoff set (`exp_cutoff/`)

Builds the Docker images for the knowledge-cutoff comparison, which runs the T6 protocol on CVEs reported after
the model's training cutoff. `static/data/post-cutoff/` holds these 20 cases, 10 assimp and 10 opensc CVEs (not
part of the benchmark; see its README).
Eleven of them are ARVO cases with public `n132/arvo:<id>-vul` images. The other nine (ids `gh-<fix commit>`,
6 assimp and 3 opensc) come from the projects' own fix commits, so their entries name a locally built
`source_image` (`local/arvo-extra:<id>-vul`) that `build_sources.py` creates. Each is built from the newest ARVO
image of its project (the entry's `base_image`), checked out at the vulnerable commit (the fix commit's parent),
with `arvo` set to AddressSanitizer and the case's fuzz target, the case's PoC at `/tmp/poc`, and an uncompiled
tree; the docstring of `build_sources.py` lists the steps.

## Building the eval images

From `static/source`, with Docker and network access to GitHub (the vulnerable commits are fetched into the
base image's clone):

```bash
docker pull n132/arvo:470183468-vul; docker pull n132/arvo:467161860-vul   # the two base images
python exp_cutoff/build_sources.py --dry-run     # the plan per case; changes nothing
python exp_cutoff/build_sources.py               # local/arvo-extra:<id>-vul for the nine gh-* cases
python build_arvo_docker.py --benchmark ../data/post-cutoff/benchmark.json --compile-timeout 3600
```

`build_sources.py` skips images that exist (`--force` rebuilds, `--ids` selects cases). `build_arvo_docker.py`
then builds the eval images `local/dfuzzbench-arvo:<id>` of all 20 cases: from `n132/arvo:<id>-vul`, or from the
entry's `source_image` where it has one, applying the case's target patch and compiling. Nothing is pushed.

To check the eval images, run each case's own PoC through them with the artifact's dry run, which uses the `poc`
field of the benchmark json instead of a model and reports, per case, whether the target was reached and the
crash reproduced. A case is part of the set only if its PoC does both (the selection rule in
`static/data/post-cutoff/README.md`).

```bash
python evaluation_arvo.py --benchmark ../data/post-cutoff/benchmark.json --dry_run
```

The commands that run the knowledge-cutoff comparison on these cases, and how its metric is computed, are in
`exp_gemini/README.md`.
