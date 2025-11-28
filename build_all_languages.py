#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
DBI.nro Multi-language Auto-Build Script
This script automatically scans all language files, NRO files, and blueprint files,
and generates corresponding language version NRO files for each matching version combination.
"""

import os
import sys
import re
import subprocess
import argparse
from pathlib import Path

# Printing statements have been moved to the top of the script

# Determine project root directory - enhanced path handling for exe mode
if getattr(sys, 'frozen', False):
    # When packaged as exe
    exe_dir = Path(os.path.dirname(sys.executable))
    # Try multiple possible locations to find project root
    possible_roots = [
        exe_dir,  # exe directory
        exe_dir.parent,  # parent directory of exe
        Path(os.getcwd())  # current working directory
    ]
    
    # Find root directory containing necessary directory structure
    PROJECT_ROOT = None
    for root in possible_roots:
        if (root / "dbi").exists() and (root / "translate").exists() and (root / "bin").exists():
            PROJECT_ROOT = root
            break
    
    # If not found, use exe directory as default
    if PROJECT_ROOT is None:
        PROJECT_ROOT = exe_dir
        print(f"Warning: Complete project structure not found, using default path: {PROJECT_ROOT}")
else:
    # Normal Python script mode
    PROJECT_ROOT = Path(__file__).parent

print(f"Project root directory: {PROJECT_ROOT}")

# Define directory paths
DBI_DIR = PROJECT_ROOT / "dbi"
TRANSLATE_DIR = PROJECT_ROOT / "translate"
BLUEPRINTS_DIR = TRANSLATE_DIR / "blueprints"
BIN_DIR = PROJECT_ROOT / "bin"
OUTPUT_DIR = PROJECT_ROOT / "output"  # Output directory for generated files

# Determine dbipatcher executable name based on operating system
if sys.platform.startswith('win'):
    DBIPATCHER_EXE = BIN_DIR / "dbipatcher.exe"
else:
    DBIPATCHER_EXE = BIN_DIR / "dbipatcher"

# Ensure paths are in string format (compatible with Windows)
DBIPATCHER_EXE = str(DBIPATCHER_EXE)
DBI_DIR = str(DBI_DIR)
BLUEPRINTS_DIR = str(BLUEPRINTS_DIR)
TRANSLATE_DIR = str(TRANSLATE_DIR)
OUTPUT_DIR = str(OUTPUT_DIR)  # Add output directory to string conversion

# Check if dbipatcher tool exists
if not os.path.exists(DBIPATCHER_EXE):
    # Try other possible locations
    alternative_paths = [
        os.path.join(os.getcwd(), "bin", "dbipatcher.exe"),
        os.path.join(os.getcwd(), "dbipatcher.exe")
    ]
    
    found = False
    for alt_path in alternative_paths:
        if os.path.exists(alt_path):
            DBIPATCHER_EXE = alt_path
            found = True
            print(f"Found dbipatcher in alternative location: {DBIPATCHER_EXE}")
            break
    
    if not found:
        print(f"Error: Cannot find dbipatcher tool: {DBIPATCHER_EXE}")
        print("Please ensure dbipatcher.exe is in the bin directory, or run this program in the correct project directory")
        print("Or you can copy dbipatcher.exe to the current working directory")
        sys.exit(1)


def extract_version(filename):
    """
    Extract version number from filename
    For example, extract "845" from "DBI.845.ru.nro" or "blueprint.845.txt"
    """
    match = re.search(r'\.([0-9]+)\.', filename)
    if match:
        return match.group(1)
    return None


def extract_language(filename):
    """
    Extract language code from language filename
    For example, extract "es" from "lang.es.txt"
    """
    match = re.search(r'lang\.([a-z]+(?:-[a-z]+)?)\.txt', filename)
    if match:
        return match.group(1)
    return None


def get_russian_nros():
    """
    Get all Russian NRO files and group them by version number
    Returns dictionary: {"version": "file_path"}
    """
    nro_files = {}
    try:
        for file in os.listdir(DBI_DIR):
            if file.endswith(".ru.nro"):
                version = extract_version(file)
                if version:
                    nro_files[version] = os.path.join(DBI_DIR, file)
    except Exception as e:
        print(f"扫描NRO文件时出错: {e}")
    return nro_files


def get_blueprints():
    """
    Get all blueprint files and group them by version number
    Returns dictionary: {"version": "file_path"}
    """
    blueprint_files = {}
    try:
        for file in os.listdir(BLUEPRINTS_DIR):
            if file.startswith("blueprint.") and file.endswith(".txt"):
                version = extract_version(file)
                if version:
                    blueprint_files[version] = os.path.join(BLUEPRINTS_DIR, file)
    except Exception as e:
        print(f"扫描blueprint文件时出错: {e}")
    return blueprint_files


def get_language_files():
    """
    Get all language files
    Returns dictionary: {"language_code": "file_path"}
    """
    lang_files = {}
    try:
        for file in os.listdir(TRANSLATE_DIR):
            if file.startswith("lang.") and file.endswith(".txt"):
                lang_code = extract_language(file)
                if lang_code and lang_code != "ru":  # 排除俄语文件
                    lang_files[lang_code] = os.path.join(TRANSLATE_DIR, file)
    except Exception as e:
        print(f"扫描语言文件时出错: {e}")
    return lang_files


def run_dbipatcher(blueprint_path, nro_path, lang_path, output_path):
    """
    Run dbipatcher tool to generate the target language version NRO file
    """
    try:
        # 构建命令行
        cmd = [
            DBIPATCHER_EXE,
            "--patch", blueprint_path,
            "--nro", nro_path,
            "--lang", lang_path,
            "--out", output_path
        ]
        
        print(f"\n正在构建: {os.path.basename(output_path)}")
        print(f"命令: {' '.join(cmd)}")
        
        # 确保输出目录存在
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        
        # 执行命令，不捕获输出以避免编码问题，直接输出到控制台
        process = subprocess.Popen(cmd)
        process.wait()
        
        # 主要检查文件是否存在且大小合理（即使返回非零代码）
        if os.path.exists(output_path):
            file_size = os.path.getsize(output_path)
            if file_size > 1024:  # 确保文件大小合理（大于1KB）
                if process.returncode == 0:
                    print(f"✓ 成功: {os.path.basename(output_path)} (大小: {file_size:,} 字节)")
                else:
                    print(f"⚠ 部分成功: {os.path.basename(output_path)} (有警告，但文件已创建) (大小: {file_size:,} 字节)")
                return True
            else:
                print(f"✗ 失败: {os.path.basename(output_path)} 文件大小异常 ({file_size} 字节)")
                return False
        else:
            print(f"✗ 失败: {os.path.basename(output_path)} 文件未创建")
            print(f"  退出代码: {process.returncode}")
            return False
    except Exception as e:
        print(f"✗ 执行命令时出错: {e}")
        return False


def main():
    """
    Main function
    """
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='DBI Multi-language Auto-Build Script')
    parser.add_argument('--version', '-v', help='Filter specific DBI version (e.g., "845")')
    parser.add_argument('--language', '-l', help='Filter specific language code (e.g., "en", "zhcn")')
    args = parser.parse_args()
    
    print("=" * 60)
    print("DBI Multi-language Auto-Build Script")
    print("=" * 60)
    
    # Create output directory if it doesn't exist
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print(f"Output directory: {OUTPUT_DIR}")
    
    # Check if dbipatcher tool exists
    if not os.path.exists(DBIPATCHER_EXE):
        print(f"Error: dbipatcher tool not found: {DBIPATCHER_EXE}")
        print("Please run 'make' command to compile the project first")
        return 1
    
    # Get all files
    print("\nScanning files...")
    russian_nros = get_russian_nros()
    blueprints = get_blueprints()
    language_files = get_language_files()
    
    print(f"Found {len(russian_nros)} Russian NRO files")
    print(f"Found {len(blueprints)} blueprint files")
    print(f"Found {len(language_files)} language files: {', '.join(language_files.keys())}")
    
    # Find matching versions
    common_versions = set(russian_nros.keys()) & set(blueprints.keys())
    if not common_versions:
        print("Error: No matching NRO and blueprint versions found")
        return 1
    
    print(f"Found {len(common_versions)} matching versions: {', '.join(sorted(common_versions))}")
    
    # Apply version filter if specified
    if args.version:
        filtered_versions = [v for v in common_versions if v == args.version]
        if not filtered_versions:
            print(f"Warning: Version {args.version} not found in matching versions")
            return 1
        common_versions = filtered_versions
        print(f"Filtered to version: {args.version}")
    
    # Apply language filter if specified
    if args.language:
        if args.language in language_files:
            filtered_languages = {args.language: language_files[args.language]}
            language_files = filtered_languages
            print(f"Filtered to language: {args.language}")
        else:
            print(f"Warning: Language {args.language} not found in available languages")
            return 1
    
    # Start building
    print("\nStarting multi-language build...")
    print("-" * 60)
    
    total_builds = len(common_versions) * len(language_files)
    successful_builds = 0
    
    for version in sorted(common_versions):
        for lang_code, lang_path in language_files.items():
            # Build output filename
            output_filename = f"DBI.{version}.{lang_code}.nro"
            output_path = os.path.join(OUTPUT_DIR, output_filename)
            
            # 调用dbipatcher
            if run_dbipatcher(blueprints[version], russian_nros[version], lang_path, output_path):
                successful_builds += 1
    
    # Output statistics
    print("\n" + "-" * 60)
    print(f"Build completed: {successful_builds}/{total_builds} successful")
    
    if successful_builds == total_builds:
        print("✓ All builds completed successfully!")
    else:
        print("! Some builds failed, please check error messages")
    
    print("=" * 60)
    return 0 if successful_builds > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
