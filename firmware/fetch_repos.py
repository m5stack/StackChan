import os
import subprocess
import json


def apply_repo_patch(path, patch_path):
    # Normalize Windows patch checkouts in memory; Git still handles the
    # dependency's own checkout line-ending rules.
    with open(patch_path, "rb") as patch_file:
        patch_data = patch_file.read().replace(b"\r\n", b"\n")

    command = ["git", "-C", path, "apply"]
    check_result = subprocess.run(
        command + ["--check", "-"], input=patch_data, capture_output=True
    )
    if check_result.returncode == 0:
        subprocess.run(command + ["-"], input=patch_data, check=True)
        print(f"Applied patch {patch_path} to {path}")
        return

    reverse_result = subprocess.run(
        command + ["--reverse", "--check", "-"],
        input=patch_data,
        capture_output=True,
    )
    if reverse_result.returncode == 0:
        print(f"Patch {patch_path} is already applied to {path}")
        return

    detail = check_result.stderr.decode(errors="replace").strip()
    raise RuntimeError(
        f"Patch {patch_path} cannot be applied to {path} and is not already applied. "
        f"Resolve the dependency changes before retrying.\n{detail}"
    )


def clone_or_update_repo(
    repo_url, path, ref=None, with_submodules=False, patch_path=None
):
    if not os.path.exists(path):
        subprocess.run(["git", "clone", repo_url, path], check=True)
    else:
        subprocess.run(["git", "-C", path, "fetch"], check=True)

    if ref:
        subprocess.run(["git", "-C", path, "checkout", ref], check=True)

    if with_submodules:
        subprocess.run(
            ["git", "-C", path, "submodule", "update", "--init", "--recursive"],
            check=True,
        )

    # 应用 patch
    if patch_path:
        patch_full_path = (
            patch_path
            if os.path.isabs(patch_path)
            else os.path.join(os.getcwd(), patch_path)
        )
        apply_repo_patch(path, patch_full_path)


def fetch_dependencies():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(script_dir, "repos.json")

    with open(config_path) as f:
        repos = json.load(f)

    for repo in repos:
        repo_path = os.path.join(script_dir, repo["path"])
        branch = repo.get("branch")
        with_submodules = repo.get("with_submodules", False)
        patch = repo.get("patch")
        if patch and not os.path.isabs(patch):
            patch = os.path.join(script_dir, patch)
        clone_or_update_repo(repo["url"], repo_path, branch, with_submodules, patch)


if __name__ == "__main__":
    fetch_dependencies()
