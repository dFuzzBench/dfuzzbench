OSS-Fuzz base images (Apache-2.0, from OSS-Fuzz's `infra/base-images`).

Only `base-runner` is rebuilt for this artifact: it adds `run_fuzzer_new`, which runs the instrumented harness
for up to 600 s and writes `/out/fuzzer_output.log`. From `static/`:

```bash
bash docker-utils/base-images/all.sh   # tags the result gcr.io/oss-fuzz-base/base-runner
```

The harness images build `FROM gcr.io/oss-fuzz-base/base-builder` / `base-builder-python` as published by
OSS-Fuzz; the other directories are kept for reference and are not built.
