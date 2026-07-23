#!/usr/bin/env python3

import os
import sys

import pytest

pytest.importorskip("neug")

from neug import Database  # noqa: E402


@pytest.fixture
def conn(tmp_path):
    ext_home = os.environ.get("NEUG_EXTENSION_HOME_PYENV")
    if not ext_home:
        build_release = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..", "build", "release")
        )
        if os.path.isdir(build_release):
            os.environ["NEUG_EXTENSION_HOME_PYENV"] = build_release
        else:
            pytest.skip(
                "Set NEUG_EXTENSION_HOME_PYENV to the NeuG build dir containing extension/"
            )

    db = Database(str(tmp_path / "ldbc_db"))
    connection = db.connect()
    connection.execute("LOAD ldbc")
    yield connection
    db.close()


def test_wiggle_scalar(conn):
    result = conn.execute("RETURN wiggle('Sam')")
    assert result is not None
