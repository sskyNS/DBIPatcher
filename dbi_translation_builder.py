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
    
    # 首先检查可执行文件是否存在
    if not check_dbipatcher_installed():
        return False
    
    # 检查是否已经存在转换好的基础文本文件
    base_text_file = "translate/rec6.810.ru.txt"
    if os.path.exists(base_text_file):
        print(f"[INFO] Base text file already exists: {base_text_file}")
        print("[INFO] Skipping extraction and conversion of base file")
    else:
        # 提取基础文件
        print("[INFO] Extracting base DBI file...")
        if not run_dbipatcher_command(["--extract", "dbi/DBI.810.ru.nro", "--output", "temp/DBI_810"]):
            return False
        
        # 转换基础文件
        print("[INFO] Converting base binary to text...")
        if not run_dbipatcher_command(["--convert", "temp/DBI_810/rec6.bin", "--output", base_text_file, "--keys", "temp/DBI_810/keys_ru.txt"]):
            print("[WARNING] Base conversion failed, but continuing with existing languages")
            # 如果基础转换失败，但仍然尝试构建其他语言
    
    # 获取可用语言列表（包括基础语言）
    trans_files = []
    if os.path.exists("translate"):
        trans_files = [f for f in os.listdir("translate") if f.startswith("rec6.810.") and f.endswith(".txt")]
    
    languages = [f.replace("rec6.810.", "").replace(".txt", "") for f in trans_files]
    
    # 移除基础语言，只处理翻译语言
    if "ru" in languages:
        languages.remove("ru")
    
    print(f"[INFO] Found {len(languages)} translation languages: {', '.join(languages)}")
    
    if len(languages) == 0:
        print("[ERROR] No translation files found in translate/ directory")
        return False
    
    # 为每种语言构建翻译
    success_count = 0
    for lang in languages:
        print(f"[INFO] Building translation for language: {lang}")
        
        try:
            # 确保输出目录存在
            os.makedirs("temp/DBI_810/bin", exist_ok=True)
            
            # 提取keys
            keys_file = f"temp/DBI_810/keys_{lang}.txt"
            if not os.path.exists(keys_file):
                if not run_dbipatcher_command(["--extract-keys", "dbi/DBI.810.ru.nro", "--output", keys_file, "--lang", lang]):
                    continue
            
            # 转换翻译文件
            bin_file = f"temp/DBI_810/rec6.{lang}.bin"
            if not run_dbipatcher_command(["--convert", f"translate/rec6.810.{lang}.txt", "--output", bin_file, "--keys", keys_file]):
                continue
            
            # 打补丁生成最终文件
            output_file = f"temp/DBI_810/bin/DBI.810.{lang}.nro"
            if not run_dbipatcher_command(["--patch", bin_file, "--binary", "dbi/DBI.810.ru.nro", "--output", output_file, "--slot", "6"]):
                continue
            
            success_count += 1
            print(f"[SUCCESS] Completed translation for {lang}")
            
        except Exception as e:
            print(f"[ERROR] Failed to build translation for {lang}: {str(e)}")
            continue
    
    return success_count > 0

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
