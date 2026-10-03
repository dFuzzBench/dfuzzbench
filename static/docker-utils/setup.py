from setuptools import setup

# Install editable (pip install -e docker-utils/): DFUZZBENCH_DIR, where /out dirs go, is derived from the
# package's location in this checkout.
setup(
    name="docker_utils",
    version="0.1",
    packages=["docker_utils"],
)
