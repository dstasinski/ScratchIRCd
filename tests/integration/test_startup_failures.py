#!/usr/bin/env python3
"""Process-level fail-closed coverage for unsafe startup configurations."""

import os
import socket
import subprocess
import sys
import tempfile


def base_config(directory, port, operators_db=None):
    database_paths = {
        name: os.path.join(directory, f"{name}.db")
        for name in ("operators", "bans", "nickserv", "chanserv", "memoserv", "history")
    }
    if operators_db is not None:
        database_paths["operators"] = operators_db
    lines = [
        "server_name = test.local",
        "network_name = TestNet",
        "bind_address = 127.0.0.1",
        f"port = {port}",
        "max_clients = 16",
        "dns_timeout_seconds = 1",
        "geoip_city_db =",
        "geoip_asn_db =",
    ]
    lines.extend(f"{name}_db = {path}" for name, path in database_paths.items())
    return lines


def write_config(path, lines):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def expect_startup_failure(binary, config_path, expected):
    try:
        result = subprocess.run(
            [binary, config_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5.0,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(
            f"server remained running for invalid configuration {config_path}: "
            f"stdout={error.stdout!r}, stderr={error.stderr!r}"
        ) from error
    assert result.returncode != 0, (
        f"server accepted invalid configuration {config_path}: "
        f"stdout={result.stdout!r}, stderr={result.stderr!r}"
    )
    for needle in expected:
        assert needle in result.stderr, (
            f"expected {needle!r} in startup diagnostics; "
            f"stderr={result.stderr!r}"
        )


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_startup_failures.py scratchircd")
    binary = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory(prefix="scratchircd-startup-") as directory:
        malformed = os.path.join(directory, "malformed.conf")
        write_config(malformed, ["server_name test.local"])
        expect_startup_failure(
            binary, malformed,
            ["expected key=value", "Failed to load configuration"],
        )

        invalid_value = os.path.join(directory, "invalid-value.conf")
        write_config(invalid_value, ["max_clients = 0"])
        expect_startup_failure(
            binary, invalid_value,
            ["invalid option 'max_clients'", "Failed to load configuration"],
        )

        incomplete_tls = os.path.join(directory, "incomplete-tls.conf")
        incomplete_lines = base_config(directory, 1)
        incomplete_lines.append(f"tls_cert_file = {directory}/server.pem")
        write_config(incomplete_tls, incomplete_lines)
        expect_startup_failure(
            binary, incomplete_tls,
            ["TLS requires both tls_cert_file and tls_key_file", "Failed to start"],
        )

        invalid_tls = os.path.join(directory, "invalid-tls.conf")
        missing_cert = os.path.join(directory, "missing-cert.pem")
        invalid_tls_lines = base_config(directory, 1)
        invalid_tls_lines.extend([
            f"tls_cert_file = {missing_cert}",
            f"tls_key_file = {directory}/missing-key.pem",
        ])
        write_config(invalid_tls, invalid_tls_lines)
        expect_startup_failure(
            binary, invalid_tls,
            [f"Failed to load TLS certificate chain: {missing_cert}", "Failed to start"],
        )

        blocked_parent = os.path.join(directory, "not-a-directory")
        with open(blocked_parent, "w", encoding="utf-8") as handle:
            handle.write("blocks database creation\n")
        blocked_database = os.path.join(blocked_parent, "operators.db")
        invalid_database = os.path.join(directory, "invalid-database.conf")
        write_config(
            invalid_database,
            base_config(directory, 1, operators_db=blocked_database),
        )
        expect_startup_failure(
            binary, invalid_database,
            [f"Failed to open operator database: {blocked_database}"],
        )

        occupied = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        occupied.bind(("127.0.0.1", 0))
        occupied.listen(1)
        occupied_port = occupied.getsockname()[1]
        try:
            listener_conflict = os.path.join(directory, "listener-conflict.conf")
            write_config(
                listener_conflict,
                base_config(directory, occupied_port),
            )
            expect_startup_failure(
                binary, listener_conflict,
                [
                    f"Failed to create plaintext listener on 127.0.0.1:{occupied_port}",
                    "Failed to start",
                ],
            )
        finally:
            occupied.close()

    print("startup failure integration test passed")


if __name__ == "__main__":
    main()
