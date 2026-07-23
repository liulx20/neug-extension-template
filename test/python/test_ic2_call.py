#!/usr/bin/env python3
"""End-to-end Python test for CALL ic2(...) via the ldbc extension."""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
import traceback

# Paths relative to this file (neug-extension-template/test/python/).
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TEMPLATE_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
NEUG_ROOT = os.path.abspath(os.path.join(TEMPLATE_ROOT, "..", "neug"))
EXT_BUILD = os.path.join(TEMPLATE_ROOT, "build", "release")
EXT_LIB = os.path.join(EXT_BUILD, "extension", "ldbc", "libldbc.neug_extension")
PYBIND_BUILD = os.path.join(NEUG_ROOT, "tools", "python_bind", "build", "lib.linux-x86_64-3.9")
MIMALLOC_DIR = os.path.join(EXT_BUILD, "third_party", "mimalloc")


def setup_env() -> None:
    pybind_dirs = [
        PYBIND_BUILD,
        os.path.join(NEUG_ROOT, "build", "tools", "python_bind"),
    ]
    for d in pybind_dirs:
        if os.path.isdir(d) and d not in sys.path:
            sys.path.insert(0, d)

    pybind_pkg = os.path.join(NEUG_ROOT, "tools", "python_bind")
    if pybind_pkg not in sys.path:
        sys.path.insert(0, pybind_pkg)

    ld_paths = [p for p in (PYBIND_BUILD, MIMALLOC_DIR) if os.path.isdir(p)]
    if ld_paths:
        os.environ["LD_LIBRARY_PATH"] = ":".join(ld_paths + [os.environ.get("LD_LIBRARY_PATH", "")])


def neug_timestamp_ms(conn, ts_str: str) -> int:
    """Convert LDBC-style timestamp string to NeuG internal millis."""
    res = conn.execute(f"RETURN cast(timestamp('{ts_str}') AS INT64)")
    return int(next(res)[0])


def setup_mini_graph(conn) -> None:
    queries = [
      "CREATE NODE TABLE PERSON(id INT64, firstName STRING, lastName STRING, PRIMARY KEY(id));",
      "CREATE NODE TABLE POST(id INT64, imageFile STRING, content STRING, length INT32, creationDate TIMESTAMP, PRIMARY KEY(id));",
      "CREATE NODE TABLE COMMENT(id INT64, content STRING, creationDate TIMESTAMP, PRIMARY KEY(id));",
      "CREATE REL TABLE KNOWS(FROM PERSON TO PERSON);",
      "CREATE REL TABLE HASCREATOR(FROM POST TO PERSON, FROM COMMENT TO PERSON, creationDate TIMESTAMP);",
      "CREATE (root:PERSON {id: 1, firstName: 'A', lastName: 'Root'}), (friend:PERSON {id: 2, firstName: 'B', lastName: 'Friend'});",
      "MATCH (a:PERSON {id:1}), (b:PERSON {id:2}) CREATE (a)-[:KNOWS]->(b);",
      "CREATE (post:POST {id: 100, imageFile: '', content: 'hello', length: 5, creationDate: timestamp('2012-04-09 18:45:05.842')});",
      "MATCH (p:POST {id:100}), (f:PERSON {id:2}) CREATE (p)-[:HASCREATOR {creationDate: timestamp('2012-04-09 18:45:05.842')}]->(f);",
    ]
    for q in queries:
        conn.execute(q)


def run_ic2_call(conn, person_id: int, max_date_ms: int):
    query = (
        f"CALL ic2({person_id}, {max_date_ms}) "
        "YIELD personId, personFirstName, personLastName, messageId, "
        "messageContent, messageCreationDate "
        "RETURN personId, personFirstName, personLastName, messageId, "
        "messageContent, messageCreationDate"
    )
    print(f"\n>>> {query}")
    return conn.execute(query)


def main() -> int:
    setup_env()

    if not os.path.isfile(EXT_LIB):
        print(f"ERROR: extension not built: {EXT_LIB}")
        print("Run: cd neug-extension-template && make")
        return 1

    try:
        import neug
        from neug import Database
    except ImportError as exc:
        print(f"ERROR: cannot import neug: {exc}")
        print("Activate venv and ensure python_bind is built.")
        return 1

    print(f"neug version: {neug.__version__}")
    print(f"extension: {EXT_LIB}")

    db_path = tempfile.mkdtemp(prefix="ic2_py_test_")
    try:
        db = Database(db_path)
        conn = db.connect()

        print("\n[1/4] setup mini LDBC graph")
        setup_mini_graph(conn)

        print("\n[2/4] LOAD extension")
        conn.execute(f"LOAD '{EXT_LIB}'")

        print("\n[3/4] resolve maxDate millis")
        max_date_str = "2012-04-11 00:00:00.000"
        max_date_ms = neug_timestamp_ms(conn, max_date_str)
        print(f"maxDate '{max_date_str}' -> {max_date_ms} ms")

        print("\n[4/4] CALL ic2 with INT64 literals")
        result = run_ic2_call(conn, person_id=1, max_date_ms=max_date_ms)
        rows = list(result)
        print(f"rows: {len(rows)}")
        for row in rows:
            print(" ", row)

        assert len(rows) == 1, f"expected 1 row, got {len(rows)}"
        person_id, first_name, last_name, message_id, content, _creation_date = rows[0]
        assert int(person_id) == 2, f"personId expected 2, got {person_id}"
        assert str(first_name) == "B", f"personFirstName expected B, got {first_name}"
        assert int(message_id) == 100, f"messageId expected 100, got {message_id}"
        assert str(content) == "hello", f"messageContent expected hello, got {content}"

        print("\n✓ IC2 Python test passed")
        return 0

    except Exception:
        traceback.print_exc()
        return 1

    finally:
        try:
            conn.close()
            db.close()
        except Exception:
            pass
        shutil.rmtree(db_path, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
