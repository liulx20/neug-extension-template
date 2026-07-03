#!/usr/bin/env python3

import os
import re
import shutil
import sys
from pathlib import Path


def is_snake_case(s: str) -> bool:
    pattern = r"^[a-z0-9]+(_[a-z0-9]+)*$"
    return bool(re.match(pattern, s))


def to_camel_case(snake_str: str) -> str:
    return "".join(x.capitalize() for x in snake_str.lower().split("_"))


def replace(file_name: str, to_find: str, to_replace: str) -> None:
    with open(file_name, "r", encoding="utf8") as file:
        filedata = file.readlines()

    new_filedata = []
    for line in filedata:
        if "__REPLACEMENT_DONE__" in line:
            new_filedata.append(line)
            continue

        modified_line = line.replace(to_find, to_replace)
        modified_line = modified_line.replace(
            to_find.capitalize(), to_camel_case(to_replace)
        )
        modified_line = modified_line.replace(to_find.upper(), to_replace.upper())

        if to_find in line or to_find.capitalize() in line or to_find.upper() in line:
            modified_line += "__REPLACEMENT_DONE__"

        new_filedata.append(modified_line)

    with open(file_name, "w", encoding="utf8") as file:
        file.writelines(new_filedata)


def replace_everywhere(to_find: str, to_replace: str) -> None:
    for path in files_to_search:
        replace(path, to_find, to_replace)

    for path in [
        "./CMakeLists.txt",
        "./Makefile",
        "./README.md",
        "./extension_config.cmake",
        ".github/workflows/ExtensionTemplate.yml",
        ".github/workflows/MainDistributionPipeline.yml",
    ]:
        if os.path.exists(path):
            replace(path, to_find, to_replace)


def replace_placeholders(file_name: str) -> None:
    with open(file_name, "r", encoding="utf8") as file:
        filedata = file.read()
    filedata = filedata.replace("__REPLACEMENT_DONE__", "")
    with open(file_name, "w", encoding="utf8") as file:
        file.write(filedata)


def remove_placeholder() -> None:
    for path in files_to_search:
        replace_placeholders(path)
    for path in [
        "./CMakeLists.txt",
        "./Makefile",
        "./README.md",
        "./extension_config.cmake",
        ".github/workflows/ExtensionTemplate.yml",
        ".github/workflows/MainDistributionPipeline.yml",
    ]:
        if os.path.exists(path):
            replace_placeholders(path)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise Exception(
            "usage: python3 bootstrap-template.py <name_for_extension_in_snake_case>"
        )

    name_extension = sys.argv[1]

    if name_extension[0].isdigit():
        raise Exception("Please dont start your extension name with a number.")

    if not is_snake_case(name_extension):
        raise Exception(
            "Please enter the name of your extension in valid snake_case containing only lower case letters and numbers"
        )

    shutil.copyfile("docs/NEXT_README.md", "README.md")
    os.remove("docs/NEXT_README.md")
    if os.path.exists("docs/README.md"):
        os.remove("docs/README.md")

    files_to_search = []
    files_to_search.extend(Path("./.github").rglob("*.yml"))
    files_to_search.extend(Path("./test").rglob("*"))
    files_to_search.extend(Path("./src").rglob("*"))
    files_to_search.extend(Path("./include").rglob("*"))

    replace_everywhere("wiggle", name_extension)
    replace_everywhere("<extension_name>", name_extension)

    remove_placeholder()

    string_to_replace = name_extension
    string_to_find = "wiggle"

    os.rename(f"test/{string_to_find}_test.cc", f"test/{string_to_replace}_test.cc")
    if os.path.exists(f"test/python/test_{string_to_find}.py"):
        os.rename(
            f"test/python/test_{string_to_find}.py",
            f"test/python/test_{string_to_replace}.py",
        )
    os.rename(
        f"src/{string_to_find}_extension.cc", f"src/{string_to_replace}_extension.cc"
    )
    os.rename(
        f"include/{string_to_find}_function.h",
        f"include/{string_to_replace}_function.h",
    )

    if os.path.exists(".github/workflows/ExtensionTemplate.yml"):
        os.remove(".github/workflows/ExtensionTemplate.yml")

    os.remove(__file__)
