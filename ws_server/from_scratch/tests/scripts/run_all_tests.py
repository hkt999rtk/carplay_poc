#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path


class StepError(Exception):
    pass


def parse_args():
    parser = argparse.ArgumentParser(
        description="Build project and run WebSocket stress scenarios.")
    parser.add_argument("--root", type=Path, required=True,
                        help="Repository root directory.")
    parser.add_argument("--build", type=Path, required=True,
                        help="CMake build directory.")
    parser.add_argument("--report", type=Path, required=True,
                        help="Path to aggregated report JSON.")
    parser.add_argument("--log-dir", type=Path, required=True,
                        help="Directory for stdout/stderr logs.")
    parser.add_argument("--report-dir", type=Path, required=True,
                        help="Directory for per-scenario reports.")
    parser.add_argument("--python", default=sys.executable,
                        help="Python interpreter for helper scripts (reserved).")
    return parser.parse_args()


def run_command(cmd, cwd, log_base, env=None):
    start = time.time()
    result = subprocess.run(
        cmd,
        cwd=cwd,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )
    duration = time.time() - start

    stdout_path = log_base.with_suffix(".stdout.log")
    stderr_path = log_base.with_suffix(".stderr.log")
    stdout_path.write_text(result.stdout or "", encoding="utf-8")
    stderr_path.write_text(result.stderr or "", encoding="utf-8")

    return {
        "command": cmd,
        "returncode": result.returncode,
        "stdout_log": str(stdout_path),
        "stderr_log": str(stderr_path),
        "duration": duration,
    }


def wait_for_ready(proc, parser, timeout, captured_stdout):
    start = time.time()
    while True:
        if time.time() - start > timeout:
            raise StepError("timeout waiting for ready signal")

        line = proc.stdout.readline()
        if not line:
            if proc.poll() is not None:
                raise StepError("process exited before ready signal")
            time.sleep(0.05)
            continue

        captured_stdout.append(line)
        parsed = parser(line)
        if parsed is not None:
            return parsed


def parse_ready_json(line):
    try:
        payload = json.loads(line)
    except json.JSONDecodeError:
        return None
    if payload.get("event") == "ready":
        return payload
    return None

def wait_for_port(host, port, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1):
                return
        except OSError:
            time.sleep(0.05)
    raise StepError(f"timeout waiting for {host}:{port}")


def orchestrate_pair(server_cmd, client_cmd, cwd, logs_dir,
                     server_log_name, client_log_name,
                     server_report_path, client_report_path,
                     overall_timeout=60,
                     ready_timeout=15):
    server_proc = subprocess.Popen(
        server_cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    server_stdout_lines = []
    try:
        wait_for_ready(
            server_proc, parse_ready_json, ready_timeout, server_stdout_lines)
    except StepError as exc:
        server_proc.kill()
        stdout_rest, stderr_rest = server_proc.communicate()
        server_stdout_lines.append(stdout_rest)
        server_stderr = stderr_rest
        raise StepError(f"{server_log_name} failed to start: {exc}") from exc

    client_result = run_command(
        client_cmd,
        cwd=cwd,
        log_base=logs_dir / client_log_name,
    )

    try:
        stdout_rest, stderr_rest = server_proc.communicate(
            timeout=max(1, overall_timeout - ready_timeout))
    except subprocess.TimeoutExpired:
        server_proc.kill()
        stdout_rest, stderr_rest = server_proc.communicate()

    server_stdout_lines.append(stdout_rest)
    server_stdout = "".join(server_stdout_lines)
    server_stderr = stderr_rest

    server_stdout_path = logs_dir / f"{server_log_name}.stdout.log"
    server_stderr_path = logs_dir / f"{server_log_name}.stderr.log"
    server_stdout_path.write_text(server_stdout or "", encoding="utf-8")
    server_stderr_path.write_text(server_stderr or "", encoding="utf-8")

    server_returncode = server_proc.returncode
    scenario_passed = (
        server_returncode == 0 and client_result["returncode"] == 0)

    server_report = Path(server_report_path)
    client_report = Path(client_report_path)
    reports = []
    if server_report.is_file():
        reports.append(str(server_report))
    if client_report.is_file():
        reports.append(str(client_report))

    return {
        "server_stdout_log": str(server_stdout_path),
        "server_stderr_log": str(server_stderr_path),
        "client_stdout_log": client_result["stdout_log"],
        "client_stderr_log": client_result["stderr_log"],
        "server_returncode": server_returncode,
        "client_returncode": client_result["returncode"],
        "status": "passed" if scenario_passed else "failed",
        "reports": reports,
    }

def run_server_client_with_port_wait(server_cmd, client_cmd, cwd, logs_dir,
                                     server_log_name, client_log_name,
                                     port_host, port_number,
                                     overall_timeout=60,
                                     ready_timeout=15):
    server_proc = subprocess.Popen(
        server_cmd,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    try:
        wait_for_port(port_host, port_number, ready_timeout)
    except StepError as exc:
        server_proc.kill()
        stdout_rest, stderr_rest = server_proc.communicate()
        server_stdout_path = logs_dir / f"{server_log_name}.stdout.log"
        server_stderr_path = logs_dir / f"{server_log_name}.stderr.log"
        server_stdout_path.write_text(stdout_rest or "", encoding="utf-8")
        server_stderr_path.write_text(stderr_rest or "", encoding="utf-8")
        raise StepError(f"{server_log_name} failed to open port: {exc}") from exc

    client_result = run_command(
        client_cmd,
        cwd=cwd,
        log_base=logs_dir / client_log_name,
    )

    if server_proc.poll() is None:
        try:
            server_proc.send_signal(signal.SIGTERM)
        except ProcessLookupError:
            pass

    try:
        stdout_rest, stderr_rest = server_proc.communicate(
            timeout=max(1, overall_timeout - ready_timeout))
    except subprocess.TimeoutExpired:
        server_proc.kill()
        stdout_rest, stderr_rest = server_proc.communicate()

    server_stdout_path = logs_dir / f"{server_log_name}.stdout.log"
    server_stderr_path = logs_dir / f"{server_log_name}.stderr.log"
    server_stdout_path.write_text(stdout_rest or "", encoding="utf-8")
    server_stderr_path.write_text(stderr_rest or "", encoding="utf-8")

    server_returncode = server_proc.returncode
    scenario_passed = client_result["returncode"] == 0

    return {
        "server_stdout_log": str(server_stdout_path),
        "server_stderr_log": str(server_stderr_path),
        "client_stdout_log": client_result["stdout_log"],
        "client_stderr_log": client_result["stderr_log"],
        "server_returncode": server_returncode,
        "client_returncode": client_result["returncode"],
        "status": "passed" if scenario_passed else "failed",
        "reports": [],
    }


def get_free_port(host="127.0.0.1"):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, 0))
        return sock.getsockname()[1]


def ensure_directories(*paths):
    for path in paths:
        path.mkdir(parents=True, exist_ok=True)


def main():
    args = parse_args()
    root = args.root.resolve()
    build_dir = args.build.resolve()
    logs_dir = args.log_dir.resolve()
    reports_dir = args.report_dir.resolve()
    ensure_directories(logs_dir, reports_dir)

    summary = []
    overall_success = True

    js_dir = root / "tests" / "js"
    npm = run_command(
        ["npm", "install"],
        cwd=js_dir,
        log_base=logs_dir / "npm_install",
    )
    summary.append({
        "name": "npm_install",
        "status": "passed" if npm["returncode"] == 0 else "failed",
        "duration_s": npm["duration"],
        "logs": {
            "stdout": npm["stdout_log"],
            "stderr": npm["stderr_log"],
        },
    })
    if npm["returncode"] != 0:
        overall_success = False

    cmake_config = run_command(
        ["cmake", "-S", str(root), "-B", str(build_dir)],
        cwd=root,
        log_base=logs_dir / "cmake_configure",
    )
    summary.append({
        "name": "cmake_configure",
        "status": "passed" if cmake_config["returncode"] == 0 else "failed",
        "duration_s": cmake_config["duration"],
        "logs": {
            "stdout": cmake_config["stdout_log"],
            "stderr": cmake_config["stderr_log"],
        },
    })
    if cmake_config["returncode"] != 0:
        overall_success = False

    cmake_build = run_command(
        ["cmake", "--build", str(build_dir)],
        cwd=root,
        log_base=logs_dir / "cmake_build",
    )
    summary.append({
        "name": "cmake_build",
        "status": "passed" if cmake_build["returncode"] == 0 else "failed",
        "duration_s": cmake_build["duration"],
        "logs": {
            "stdout": cmake_build["stdout_log"],
            "stderr": cmake_build["stderr_log"],
        },
    })
    if cmake_build["returncode"] != 0:
        overall_success = False

    ctest = run_command(
        ["ctest", "--output-on-failure"],
        cwd=build_dir,
        log_base=logs_dir / "ctest",
    )
    summary.append({
        "name": "ctest",
        "status": "passed" if ctest["returncode"] == 0 else "failed",
        "duration_s": ctest["duration"],
        "logs": {
            "stdout": ctest["stdout_log"],
            "stderr": ctest["stderr_log"],
        },
    })
    if ctest["returncode"] != 0:
        overall_success = False

    client_counts = [1, 2, 3, 4]
    mode_matrix = [
        ("concurrent", 0),
        ("sequential", 50),
    ]

    c2c_binary = build_dir / "tests" / "wsfs_c2c_stress"
    for clients in client_counts:
        for mode, stagger in mode_matrix:
            report_path = reports_dir / f"c2c_{mode}_c{clients}.json"
            log_base = logs_dir / f"c2c_{mode}_c{clients}"
            cmd = [
                str(c2c_binary),
                "--report", str(report_path),
                "--clients", str(clients),
                "--mode", mode,
            ]
            if stagger > 0:
                cmd += ["--stagger-ms", str(stagger)]

            result = run_command(cmd, cwd=root, log_base=log_base)
            summary.append({
                "name": f"c_server_c_client_{mode}_c{clients}",
                "status": "passed" if result["returncode"] == 0 else "failed",
                "duration_s": result["duration"],
                "reports": [str(report_path)] if report_path.is_file() else [],
                "logs": {
                    "stdout": result["stdout_log"],
                    "stderr": result["stderr_log"],
                },
            })
            if result["returncode"] != 0:
                overall_success = False

    valgrind_path = shutil.which("valgrind")
    if valgrind_path is not None:
        vg_report = reports_dir / "c2c_valgrind.json"
        vg_log = logs_dir / "c2c_valgrind.memcheck.log"
        vg_cmd = [
            valgrind_path,
            "--tool=memcheck",
            "--leak-check=full",
            "--show-leak-kinds=all",
            "--error-exitcode=86",
            f"--log-file={vg_log}",
            str(c2c_binary),
            "--report", str(vg_report),
            "--clients", "2",
            "--mode", "concurrent",
        ]
        vg_result = run_command(vg_cmd, cwd=root, log_base=logs_dir / "c2c_valgrind_cmd")
        summary.append({
            "name": "c_server_c_client_valgrind",
            "status": "passed" if vg_result["returncode"] == 0 else "failed",
            "duration_s": vg_result["duration"],
            "reports": [str(vg_report)] if vg_report.is_file() else [],
            "logs": {
                "stdout": vg_result["stdout_log"],
                "stderr": vg_result["stderr_log"],
                "memcheck": str(vg_log),
            },
        })
        if vg_result["returncode"] != 0:
            overall_success = False

    # Scenario 2: JS server + C client.
    js_server_script = root / "tests" / "js" / "src" / "server.js"
    js_client_binary = build_dir / "tests" / "wsfs_js_server_stress"
    for clients in client_counts:
        for mode, stagger in mode_matrix:
            js_server_report = reports_dir / f"js_server_{mode}_c{clients}.json"
            js_client_report = reports_dir / f"js_client_stress_{mode}_c{clients}.json"
            js_server_cmd = [
                "node",
                str(js_server_script),
                "--port", "19112",
                "--clients", str(clients),
                "--mode", mode,
                "--report", str(js_server_report),
            ]
            js_client_cmd = [
                str(js_client_binary),
                "--port", "19112",
                "--clients", str(clients),
                "--mode", mode,
                "--report", str(js_client_report),
            ]
            if stagger > 0:
                js_server_cmd += ["--stagger-ms", str(stagger)]
                js_client_cmd += ["--stagger-ms", str(stagger)]

            server_log_name = f"js_server_{mode}_c{clients}"
            client_log_name = f"js_server_client_{mode}_c{clients}"

            try:
                js_pair = orchestrate_pair(
                    js_server_cmd,
                    js_client_cmd,
                    cwd=root,
                    logs_dir=logs_dir,
                    server_log_name=server_log_name,
                    client_log_name=client_log_name,
                    server_report_path=js_server_report,
                    client_report_path=js_client_report,
                    overall_timeout=180,
                )
                summary.append({
                    "name": f"js_server_c_client_{mode}_c{clients}",
                    "status": js_pair["status"],
                    "reports": js_pair["reports"],
                    "logs": {
                        "server_stdout": js_pair["server_stdout_log"],
                        "server_stderr": js_pair["server_stderr_log"],
                        "client_stdout": js_pair["client_stdout_log"],
                        "client_stderr": js_pair["client_stderr_log"],
                    },
                })
                if js_pair["status"] != "passed":
                    overall_success = False
            except StepError as exc:
                overall_success = False
                summary.append({
                    "name": f"js_server_c_client_{mode}_c{clients}",
                    "status": "failed",
                    "error": str(exc),
                })

    if valgrind_path is not None:
        vg_server_report = reports_dir / "js_server_valgrind_server.json"
        vg_client_report = reports_dir / "js_server_valgrind_client.json"
        vg_log = logs_dir / "js_server_valgrind.memcheck.log"
        val_client_cmd = [
            valgrind_path,
            "--tool=memcheck",
            "--leak-check=full",
            "--show-leak-kinds=all",
            "--error-exitcode=86",
            f"--log-file={vg_log}",
            str(js_client_binary),
            "--port", "19112",
            "--clients", "2",
            "--mode", "concurrent",
            "--report", str(vg_client_report),
        ]
        vg_server_cmd = [
            "node",
            str(js_server_script),
            "--port", "19112",
            "--clients", "2",
            "--mode", "concurrent",
            "--report", str(vg_server_report),
        ]
        try:
            vg_pair = orchestrate_pair(
                vg_server_cmd,
                val_client_cmd,
                cwd=root,
                logs_dir=logs_dir,
                server_log_name="js_server_valgrind",
                client_log_name="js_server_client_valgrind",
                server_report_path=vg_server_report,
                client_report_path=vg_client_report,
                overall_timeout=240,
            )
            vg_status = vg_pair["status"]
            summary.append({
                "name": "js_server_c_client_valgrind",
                "status": vg_status,
                "reports": vg_pair["reports"],
                "logs": {
                    "server_stdout": vg_pair["server_stdout_log"],
                    "server_stderr": vg_pair["server_stderr_log"],
                    "client_stdout": vg_pair["client_stdout_log"],
                    "client_stderr": vg_pair["client_stderr_log"],
                    "memcheck": str(vg_log),
                },
            })
            if vg_status != "passed":
                overall_success = False
        except StepError as exc:
            overall_success = False
            summary.append({
                "name": "js_server_c_client_valgrind",
                "status": "failed",
                "error": str(exc),
            })

    # Scenario 3: C server + JS client.
    c_server_binary = build_dir / "tests" / "wsfs_c_js_client_stress"
    js_client_script = root / "tests" / "js" / "src" / "client.js"
    for clients in client_counts:
        for mode, stagger in mode_matrix:
            c_server_report = reports_dir / f"c_server_{mode}_c{clients}.json"
            js_client_report = reports_dir / f"js_client_{mode}_c{clients}.json"
            c_server_cmd = [
                str(c_server_binary),
                "--port", "19113",
                "--clients", str(clients),
                "--mode", mode,
                "--report", str(c_server_report),
            ]
            js_client_cmd = [
                "node",
                str(js_client_script),
                "--port", "19113",
                "--clients", str(clients),
                "--mode", mode,
                "--report", str(js_client_report),
            ]
            if stagger > 0:
                c_server_cmd += ["--stagger-ms", str(stagger)]
                js_client_cmd += ["--stagger-ms", str(stagger)]

            server_log_name = f"c_server_{mode}_c{clients}"
            client_log_name = f"c_server_js_client_{mode}_c{clients}"

            try:
                c_pair = orchestrate_pair(
                    c_server_cmd,
                    js_client_cmd,
                    cwd=root,
                    logs_dir=logs_dir,
                    server_log_name=server_log_name,
                    client_log_name=client_log_name,
                    server_report_path=c_server_report,
                    client_report_path=js_client_report,
                    overall_timeout=180,
                )
                summary.append({
                    "name": f"c_server_js_client_{mode}_c{clients}",
                    "status": c_pair["status"],
                    "reports": c_pair["reports"],
                    "logs": {
                        "server_stdout": c_pair["server_stdout_log"],
                        "server_stderr": c_pair["server_stderr_log"],
                        "client_stdout": c_pair["client_stdout_log"],
                        "client_stderr": c_pair["client_stderr_log"],
                    },
                })
                if c_pair["status"] != "passed":
                    overall_success = False
            except StepError as exc:
                overall_success = False
                summary.append({
                    "name": f"c_server_js_client_{mode}_c{clients}",
                    "status": "failed",
                    "error": str(exc),
                })

    # Scenario 4: wsfs_echo example server with user_client.js (captures ws module behavior).
    wsfs_echo_binary = build_dir / "examples" / "wsfs_echo"
    user_client_script = root / "tests" / "js" / "src" / "user_client.js"
    if wsfs_echo_binary.is_file():
        echo_host = "127.0.0.1"
        echo_port = get_free_port(echo_host)
        server_log_name = "wsfs_echo_example"
        client_log_name = "wsfs_echo_user_client"
        try:
            echo_pair = run_server_client_with_port_wait(
                [str(wsfs_echo_binary),
                 "--host", echo_host,
                 "--port", str(echo_port)],
                [
                    "node",
                    str(user_client_script),
                    "--host", echo_host,
                    "--port", str(echo_port),
                    "--message", "user-client-echo",
                    "--timeout-ms", "7000",
                ],
                cwd=root,
                logs_dir=logs_dir,
                server_log_name=server_log_name,
                client_log_name=client_log_name,
                port_host=echo_host,
                port_number=echo_port,
                overall_timeout=45,
            )
            summary.append({
                "name": "wsfs_echo_user_client",
                "status": echo_pair["status"],
                "logs": {
                    "server_stdout": echo_pair["server_stdout_log"],
                    "server_stderr": echo_pair["server_stderr_log"],
                    "client_stdout": echo_pair["client_stdout_log"],
                    "client_stderr": echo_pair["client_stderr_log"],
                },
                "metadata": {
                    "server_returncode": echo_pair["server_returncode"],
                    "client_returncode": echo_pair["client_returncode"],
                },
            })
            if echo_pair["status"] != "passed":
                overall_success = False
        except StepError as exc:
            overall_success = False
            summary.append({
                "name": "wsfs_echo_user_client",
                "status": "failed",
                "error": str(exc),
            })

    if valgrind_path is not None:
        vg_server_report = reports_dir / "c_server_valgrind.json"
        vg_client_report = reports_dir / "js_client_valgrind.json"
        vg_log = logs_dir / "c_server_valgrind.memcheck.log"
        vg_server_cmd = [
            valgrind_path,
            "--tool=memcheck",
            "--leak-check=full",
            "--show-leak-kinds=all",
            "--error-exitcode=86",
            f"--log-file={vg_log}",
            str(c_server_binary),
            "--port", "19113",
            "--clients", "2",
            "--mode", "concurrent",
            "--report", str(vg_server_report),
        ]
        vg_client_cmd = [
            "node",
            str(js_client_script),
            "--port", "19113",
            "--clients", "2",
            "--mode", "concurrent",
            "--report", str(vg_client_report),
        ]
        try:
            vg_pair = orchestrate_pair(
                vg_server_cmd,
                vg_client_cmd,
                cwd=root,
                logs_dir=logs_dir,
                server_log_name="c_server_valgrind",
                client_log_name="c_server_js_client_valgrind",
                server_report_path=vg_server_report,
                client_report_path=vg_client_report,
                overall_timeout=240,
            )
            summary.append({
                "name": "c_server_js_client_valgrind",
                "status": vg_pair["status"],
                "reports": vg_pair["reports"],
                "logs": {
                    "server_stdout": vg_pair["server_stdout_log"],
                    "server_stderr": vg_pair["server_stderr_log"],
                    "client_stdout": vg_pair["client_stdout_log"],
                    "client_stderr": vg_pair["client_stderr_log"],
                    "memcheck": str(vg_log),
                },
            })
            if vg_pair["status"] != "passed":
                overall_success = False
        except StepError as exc:
            overall_success = False
            summary.append({
                "name": "c_server_js_client_valgrind",
                "status": "failed",
                "error": str(exc),
            })

    report_payload = {
        "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "overall_status": "passed" if overall_success else "failed",
        "steps": summary,
    }

    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report_payload, indent=2), encoding="utf-8")

    print("=== Test Summary ===")
    for item in summary:
        name = item["name"]
        status = item["status"]
        print(f"{name:25s}: {status}")
    print("====================")

    sys.exit(0 if overall_success else 1)


if __name__ == "__main__":
    main()
