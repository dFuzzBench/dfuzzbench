import shlex
import logging
import os
import subprocess
import sys
import argparse
import uuid

logger = logging.getLogger(__name__)

DFUZZBENCH_DIR=os.path.dirname(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
# the runner built by base-images/all.sh: OSS-Fuzz's base-runner plus run_fuzzer_new
RUNNER_IMAGE = "gcr.io/oss-fuzz-base/base-runner"
# set to give every container a findable name, <prefix>-<random suffix>
CONTAINER_PREFIX = os.environ.get("DFUZZ_CONTAINER_PREFIX")

# A run_id isolates one evaluation (one target under one model/setting) from every other:
# it gets its own image tag and its own /out dir, so concurrent runs on the same project never
# overwrite each other's image, compiled fuzzer, or fuzzer_output.log. Without a run_id the
# shared names (local/dfuzzbench-<project>, out/<project>) are used.
def image_name_for(project_name, run_id=None):
  return f"local/dfuzzbench-{project_name}:{run_id}" if run_id else f"local/dfuzzbench-{project_name}"

def out_dir_for(project_name, run_id=None):
  if run_id:
    return f"{DFUZZBENCH_DIR}/out/runs/{project_name}/{run_id}"
  return f"{DFUZZBENCH_DIR}/out/{project_name}"

def cleanup_run(project_name, run_id):
  """Removes a run's image and /out dir. /out is written by root inside the containers, so it
  is deleted from a container rather than from the host."""
  subprocess.run(['docker', 'rmi', '-f', image_name_for(project_name, run_id)], capture_output=True)
  runs_dir = os.path.dirname(out_dir_for(project_name, run_id))
  if os.path.isdir(runs_dir):
    subprocess.run(['docker', 'run', '--rm', '--pull', 'never'] + _name_args() +
                   ['-v', f'{runs_dir}:/runs', RUNNER_IMAGE, 'rm', '-rf', f'/runs/{run_id}'], capture_output=True)

def _name_args():
  return ['--name', f'{CONTAINER_PREFIX}-{uuid.uuid4().hex[:12]}'] if CONTAINER_PREFIX else []

def _env_to_docker_args(env_list):
  """Turns envirnoment variable list into docker arguments."""
  return sum([['-e', v] for v in env_list], [])

def _get_command_string(command):
  """Returns a shell escaped command string."""
  return ' '.join(shlex.quote(part) for part in command)

def check_project_exists(project_dir):
    """Checks if project exists."""
    print(f"DFUZZBENCH_DIR: {DFUZZBENCH_DIR}")
    print(f"project_dir: {project_dir}")
    if not os.path.exists(project_dir):
        logger.error(f'Project dir does not exist: {project_dir}')
        return False
    
    return True

def docker_build(build_args):
  """Calls `docker build`."""
  command = ['docker', 'build']
  command.extend(build_args)
  logger.info('Running: %s.', _get_command_string(command))

  try:
    subprocess.check_call(command)
  except subprocess.CalledProcessError:
    logger.error('Docker build failed.')
    return False

  return True

def build_image_impl(project_dir, cache=True, architecture='x86_64', run_id=None):
    # TODO: build all the base images first.
  """Builds image."""
  if not check_project_exists(project_dir):
    return False

  image_name = image_name_for(project_dir.rstrip('/').split('/')[-3], run_id)
  logger.debug(f"image_name: {image_name}")
  docker_build_dir = project_dir
  dockerfile_path = os.path.join(docker_build_dir, 'Dockerfile')

  build_args = []
  if architecture == 'aarch64':
    build_args += [
        'buildx',
        'build',
        '--platform',
        'linux/arm64',
        '--progress',
        'plain',
        '--load',
    ]
  if not cache:
    build_args.append('--no-cache')

  build_args += ['-t', image_name, '--file', dockerfile_path]
  build_args.append(docker_build_dir)

  if architecture == 'aarch64':
    command = ['docker'] + build_args
    subprocess.check_call(command)
    return True
  return docker_build(build_args)

def build_image(project_dir, run_id=None):
    if build_image_impl(project_dir, run_id=run_id):
        return True
    return False

def docker_run(run_args, print_output=True, architecture='x86_64'):
  """Calls `docker run`."""
  platform = 'linux/arm64' if architecture == 'aarch64' else 'linux/amd64'
  # images are built locally (the per-run image, the runner from base-images/all.sh): never pulled
  command = [
      'docker', 'run', '--privileged', '--rm', '--shm-size=2g', '--platform', platform, '--pull', 'never'
  ] + _name_args()

  # Support environments with a TTY.
  if sys.stdin.isatty():
    command.append('-i')

  command.extend(run_args)

  logging.info('Running: %s.', _get_command_string(command))
  
  try:
    output = subprocess.check_output(command, text=True)
  except subprocess.CalledProcessError:
    return False

  return True

def docker_target_check_new(project_name, engine, language, run_id=None):
    runner_image_name = RUNNER_IMAGE
    project_out = out_dir_for(project_name, run_id)
    logger.debug(f"[docker_target_check_new] start verification:\n\tproject_name:{project_name}\n\timage_name:{runner_image_name}\n\tproject_out:{project_out}")
    # TODO: consider parsing yaml to get project info
    env = [
        'FUZZING_ENGINE=' + f'{engine}',
        'SANITIZER=' + 'address',
        'ARCHITECTURE=' + 'x86_64',
        'PROJECT_NAME=' + f'{project_name}',
        'HELPER=True',
        'FUZZING_LANGUAGE=' + f'{language}',
        'RUN_FUZZER_MODE=interactive'
    ]
    command = _env_to_docker_args(env)
    command += ['-v', f'{project_out}:/out', runner_image_name]
    command += ['run_fuzzer_new', 'fuzzer_instrumented']
    os.makedirs(project_out, exist_ok=True)  # created by us, not as root by docker
    try:
        docker_run(command)
    except subprocess.CalledProcessError as e:
        print(f"Error running the Docker container: {e}")
        print(f"Stderr: {e.stderr}")        

    log_path = f"{project_out}/fuzzer_output.log"
    if not os.path.exists(log_path):
        raise FileNotFoundError(f"no {log_path}, the fuzzer did not run (is {RUNNER_IMAGE} the image built by "
                                "docker-utils/base-images/all.sh, which adds run_fuzzer_new?)")
    # The log can reach gigabytes: scan it in chunks, as bytes (it may hold raw input bytes).
    hit_target = False
    with open(log_path, 'rb') as f:
        carry = b""
        for chunk in iter(lambda: f.read(1 << 20), b""):
            if b"HIT TARGET" in carry + chunk:
                hit_target = True
                print("Successfully hit the target line!")
                break
            carry = chunk[-16:]

    return hit_target, log_path

def build_fuzzer(project_name, engine, language, sanitizer='address', run_id=None):
    image_name = image_name_for(project_name, run_id)
    project_out = out_dir_for(project_name, run_id)
    logger.debug(f"[build_fuzzer] start verification:\n\tproject_name:{project_name}\n\timage_name:{image_name}\n\tproject_out:{project_out}")
    # TODO: consider parsing yaml to get project info
    env = [
        'FUZZING_ENGINE=' + f'{engine}',
        'SANITIZER=' + sanitizer,
        'ARCHITECTURE=' + 'x86_64',
        'PROJECT_NAME=' + f'{project_name}',
        'HELPER=True',
        'FUZZING_LANGUAGE=' + f'{language}',
    ]
    command = _env_to_docker_args(env)
    command += ['-v', f'{project_out}:/out', image_name]
    os.makedirs(project_out, exist_ok=True)  # created by us, not as root by docker
    success = False
    try:
        success = docker_run(command)

    except subprocess.CalledProcessError as e:
        print(f"Error running the docker container: {e}")
        print(f"Stderr: {e.stderr}")

    return success

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="A tool to assess whether LLMs can generate program inputs that trigger the execution of a specified target line.")
    parser.add_argument('--dir', type=str, required=True, help='The project name')
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")

    args = parser.parse_args()

    project_dir = args.dir
    build_image(project_dir)

