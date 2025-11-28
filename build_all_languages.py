#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
多语言版本DBI.nro自动构建脚本
此脚本会自动扫描所有语言文件、NRO文件和blueprint文件，并为每个匹配的版本组合生成对应的语言版本NRO文件。
"""

import os
import sys
import re
import subprocess
from pathlib import Path

# 这里的打印语句已经移到脚本顶部

# 确定项目根目录 - 增强路径处理以支持exe模式
if getattr(sys, 'frozen', False):
    # 当被打包成exe时
    exe_dir = Path(os.path.dirname(sys.executable))
    # 尝试多种可能的位置寻找项目根目录
    possible_roots = [
        exe_dir,  # exe所在目录
        exe_dir.parent,  # exe的父目录
        Path(os.getcwd())  # 当前工作目录
    ]
    
    # 查找包含必要目录结构的根目录
    PROJECT_ROOT = None
    for root in possible_roots:
        if (root / "dbi").exists() and (root / "translate").exists() and (root / "bin").exists():
            PROJECT_ROOT = root
            break
    
    # 如果没找到，使用exe所在目录作为默认
    if PROJECT_ROOT is None:
        PROJECT_ROOT = exe_dir
        print(f"警告: 未找到完整的项目结构，使用默认路径: {PROJECT_ROOT}")
else:
    # 正常Python脚本模式
    PROJECT_ROOT = Path(__file__).parent

print(f"项目根目录: {PROJECT_ROOT}")

# 定义各目录路径
DBI_DIR = PROJECT_ROOT / "dbi"
TRANSLATE_DIR = PROJECT_ROOT / "translate"
BLUEPRINTS_DIR = TRANSLATE_DIR / "blueprints"
BIN_DIR = PROJECT_ROOT / "bin"

# 根据操作系统确定dbipatcher可执行文件名
if sys.platform.startswith('win'):
    DBIPATCHER_EXE = BIN_DIR / "dbipatcher.exe"
else:
    DBIPATCHER_EXE = BIN_DIR / "dbipatcher"

# 确保路径是字符串格式（兼容Windows）
DBIPATCHER_EXE = str(DBIPATCHER_EXE)
DBI_DIR = str(DBI_DIR)
BLUEPRINTS_DIR = str(BLUEPRINTS_DIR)
TRANSLATE_DIR = str(TRANSLATE_DIR)

# 检查dbipatcher工具是否存在
if not os.path.exists(DBIPATCHER_EXE):
    # 尝试其他可能的位置
    alternative_paths = [
        os.path.join(os.getcwd(), "bin", "dbipatcher.exe"),
        os.path.join(os.getcwd(), "dbipatcher.exe")
    ]
    
    found = False
    for alt_path in alternative_paths:
        if os.path.exists(alt_path):
            DBIPATCHER_EXE = alt_path
            found = True
            print(f"在备用位置找到dbipatcher: {DBIPATCHER_EXE}")
            break
    
    if not found:
        print(f"错误: 找不到dbipatcher工具: {DBIPATCHER_EXE}")
        print("请确保dbipatcher.exe位于bin目录中，或者将此程序放在正确的项目目录中运行")
        print("或者您可以将dbipatcher.exe复制到当前工作目录")
        sys.exit(1)


def extract_version(filename):
    """
    从文件名中提取版本号
    例如从 "DBI.845.ru.nro" 或 "blueprint.845.txt" 中提取 "845"
    """
    match = re.search(r'\.([0-9]+)\.', filename)
    if match:
        return match.group(1)
    return None


def extract_language(filename):
    """
    从语言文件名中提取语言代码
    例如从 "lang.es.txt" 中提取 "es"
    """
    match = re.search(r'lang\.([a-z]+(?:-[a-z]+)?)\.txt', filename)
    if match:
        return match.group(1)
    return None


def get_russian_nros():
    """
    获取所有俄语NRO文件，并按版本号分组
    返回字典: {"版本号": "文件路径"}
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
    获取所有blueprint文件，并按版本号分组
    返回字典: {"版本号": "文件路径"}
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
    获取所有语言文件
    返回字典: {"语言代码": "文件路径"}
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
    运行dbipatcher工具生成目标语言版本的NRO文件
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
    主函数
    """
    print("=" * 60)
    print("DBI多语言版本自动构建脚本")
    print("=" * 60)
    
    # 检查dbipatcher工具是否存在
    if not os.path.exists(DBIPATCHER_EXE):
        print(f"错误: 找不到dbipatcher工具: {DBIPATCHER_EXE}")
        print("请先运行 'make' 命令编译项目")
        return 1
    
    # 获取所有文件
    print("\n正在扫描文件...")
    russian_nros = get_russian_nros()
    blueprints = get_blueprints()
    language_files = get_language_files()
    
    print(f"找到 {len(russian_nros)} 个俄语NRO文件")
    print(f"找到 {len(blueprints)} 个blueprint文件")
    print(f"找到 {len(language_files)} 个语言文件: {', '.join(language_files.keys())}")
    
    # 找出匹配的版本
    common_versions = set(russian_nros.keys()) & set(blueprints.keys())
    if not common_versions:
        print("错误: 没有找到匹配的NRO和blueprint版本")
        return 1
    
    print(f"找到 {len(common_versions)} 个匹配的版本: {', '.join(sorted(common_versions))}")
    
    # 开始构建
    print("\n开始构建多语言版本...")
    print("-" * 60)
    
    total_builds = len(common_versions) * len(language_files)
    successful_builds = 0
    
    for version in sorted(common_versions):
        for lang_code, lang_path in language_files.items():
            # 构建输出文件名
            output_filename = f"DBI.{version}.{lang_code}.nro"
            output_path = os.path.join(DBI_DIR, output_filename)
            
            # 调用dbipatcher
            if run_dbipatcher(blueprints[version], russian_nros[version], lang_path, output_path):
                successful_builds += 1
    
    # 输出统计信息
    print("\n" + "-" * 60)
    print(f"构建完成: 成功 {successful_builds}/{total_builds}")
    
    if successful_builds == total_builds:
        print("✓ 所有构建都已成功完成！")
    else:
        print("! 部分构建失败，请检查错误信息")
    
    print("=" * 60)
    return 0 if successful_builds > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
