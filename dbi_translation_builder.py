import os
import subprocess
import sys
import shutil
import argparse 
import io

def setup_encoding():
    """Set correct encoding to avoid Unicode errors"""
    if sys.platform == "win32":
        if hasattr(sys.stdout, 'buffer'):
            sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
        if hasattr(sys.stderr, 'buffer'):
            sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')
        os.environ['PYTHONIOENCODING'] = 'utf-8'

setup_encoding()

def check_dbipatcher_installed():
    """Check if dbipatcher is available"""
    if os.path.exists("bin/dbipatcher.exe"):
        return True
    else:
        print("[ERROR] dbipatcher.exe not found in bin/ directory")
        print("Please make sure the Windows build artifact is downloaded")
        return False

def run_dbipatcher_command(args):
    """Run dbipatcher command directly"""
    try:
        dbipatcher_path = os.path.join("bin", "dbipatcher.exe")
        
        if not os.path.exists(dbipatcher_path):
            print(f"[ERROR] dbipatcher.exe not found at: {dbipatcher_path}")
            return False
        
        result = subprocess.run(
            [dbipatcher_path] + args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding='utf-8',
            shell=True  # 在Windows上使用shell=True
        )

        if result.stdout:
            print(f"[DBIPATCHER OUTPUT]\n{result.stdout}")
        if result.stderr:
            print(f"[DBIPATCHER WARNING]\n{result.stderr}")
        
        if result.returncode != 0:
            print(f"[ERROR] dbipatcher execution failed with args: {args}")
            return False
        return True
    except Exception as e:
        print(f"[ERROR] Error executing dbipatcher: {str(e)}")
        return False

def delete_old_bin_files(target_dir="temp/DBI_810"):
    """Delete old binary files"""
    if not os.path.exists(target_dir):
        print(f"[INFO] Target directory {target_dir} does not exist")
        return True
    try:
        for root, _, files in os.walk(target_dir):
            for file in files:
                if file.endswith(".bin"):
                    file_path = os.path.join(root, file)
                    os.remove(file_path)
                    print(f"[INFO] Deleted old binary file: {file_path}")
        return True
    except Exception as e:
        print(f"[WARNING] Error deleting old files: {str(e)}")
        return False

def build_translations_directly():
    """Build translations directly using the downloaded dbipatcher.exe"""
    print("[INFO] Building translations using pre-built dbipatcher.exe...")
    
    # 提取基础文件
    if not run_dbipatcher_command(["--extract", "dbi/DBI.810.ru.nro", "--output", "temp/DBI_810"]):
        return False
    
    # 转换基础文件
    if not run_dbipatcher_command(["--convert", "temp/DBI_810/rec6.bin", "--output", "translate/rec6.810.ru.txt", "--keys", "temp/DBI_810/keys_ru.txt"]):
        return False
    
    # 获取可用语言列表
    trans_files = [f for f in os.listdir("translate") if f.startswith("rec6.810.") and f.endswith(".txt") and not f.startswith("rec6.810.ru.")]
    languages = [f.replace("rec6.810.", "").replace(".txt", "") for f in trans_files]
    
    print(f"[INFO] Found {len(languages)} translation languages: {', '.join(languages)}")
    
    # 为每种语言构建翻译
    for lang in languages:
        print(f"[INFO] Building translation for language: {lang}")
        
        # 提取keys
        if not run_dbipatcher_command(["--extract-keys", "dbi/DBI.810.ru.nro", "--output", f"temp/DBI_810/keys_{lang}.txt", "--lang", lang]):
            continue
        
        # 转换翻译文件
        if not run_dbipatcher_command(["--convert", f"translate/rec6.810.{lang}.txt", "--output", f"temp/DBI_810/rec6.{lang}.bin", "--keys", f"temp/DBI_810/keys_{lang}.txt"]):
            continue
        
        # 打补丁生成最终文件
        if not run_dbipatcher_command(["--patch", f"temp/DBI_810/rec6.{lang}.bin", "--binary", "dbi/DBI.810.ru.nro", "--output", f"temp/DBI_810/bin/DBI.810.{lang}.nro", "--slot", "6"]):
            continue
    
    return True

def auto_build_all(skip_build=False):
    """Non-interactive mode: Automatically execute full language build"""
    print("[INFO] Starting automatic build process for all languages...")
    
    if not skip_build:
        print("\n[STEP 1/4] Checking dbipatcher availability")
        if not check_dbipatcher_installed():
            return False
    else:
        print("\n[STEP 1/4] Skipping build check (using pre-built executable)")
    
    print("\n[STEP 2/4] Deleting old binary payloads")
    if not delete_old_bin_files():
        return False

    print("\n[STEP 3/4] Building all translation files")
    if not build_translations_directly():
        return False
    
    trans_output_dir = "temp/DBI_810/bin"
    if not os.path.exists(trans_output_dir):
        print(f"[ERROR] Translation output directory {trans_output_dir} does not exist")
        return False
    
    trans_files = [f for f in os.listdir(trans_output_dir) if f.startswith("DBI.810.") and f.endswith(".nro")]
    if len(trans_files) == 0:
        print("[ERROR] No translation files were generated")
        return False
    
    print(f"\n[SUCCESS] Automatic build completed! Generated {len(trans_files)} translation files:")
    for f in trans_files:
        f_path = os.path.join(trans_output_dir, f)
        f_size = round(os.path.getsize(f_path)/1024, 2)
        print(f"  - {f} ({f_size} KB)")
    return True

def main():
    """Main function"""
    parser = argparse.ArgumentParser(description="DBI Patcher Translation Builder")
    parser.add_argument(
        "--build-all", 
        action="store_true", 
        help="Non-interactive mode: Automatically build all languages"
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip build check and use pre-built executable"
    )
    args = parser.parse_args()

    print("\n[INFO] DBI Patcher Translation Builder")
    print("====================================")
    
    if args.build_all:
        success = auto_build_all(args.skip_build)
        sys.exit(0 if success else 1)
    else:
        print("Interactive mode not supported in CI environment")
        sys.exit(1)

if __name__ == "__main__":
    main()
