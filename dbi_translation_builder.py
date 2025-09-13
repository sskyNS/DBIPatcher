import os
import subprocess
import sys
import shutil
import argparse 
import io

def setup_encoding():
    """Set correct encoding to avoid Unicode errors"""
    if sys.platform == "win32":
        # Set UTF-8 encoding for Windows systems
        if hasattr(sys.stdout, 'buffer'):
            sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
        if hasattr(sys.stderr, 'buffer'):
            sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')
        
        # Set environment variable
        os.environ['PYTHONIOENCODING'] = 'utf-8'

# Call at script start
setup_encoding()

def check_make_installed():
    """Check if make is installed"""
    try:
        result = subprocess.run(
            ["make", "--version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding='utf-8'
        )
        return True
    except FileNotFoundError:
        return False

def run_make_command(target=None):
    """Run make command with optional target"""
    try:
        args = ["make", "--no-print-directory"]
        if target:
            args.append(target)
            
        result = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding='utf-8',
            shell=True
        )

        if result.stdout:
            print(f"[MAKE OUTPUT]\n{result.stdout}")
        if result.stderr:
            print(f"[MAKE WARNING]\n{result.stderr}")
        
        if result.returncode != 0:
            print(f"[ERROR] make {target if target else ''} execution failed")
            return False
        return True
    except Exception as e:
        print(f"[ERROR] Error executing make command: {str(e)}")
        return False

def delete_old_bin_files(target_dir="temp/DBI_810"):
    """Delete old binary files (core solution for termination sequence error)
    Avoid tool misjudging conversion direction (text→binary vs binary→text)
    """
    if not os.path.exists(target_dir):
        print(f"[INFO] Target directory {target_dir} does not exist, no need to delete old files")
        return True
    try:
        for root, _, files in os.walk(target_dir):
            for file in files:
                if file.endswith(".bin"):  # Only delete binary payload files
                    file_path = os.path.join(root, file)
                    os.remove(file_path)
                    print(f"[INFO] Deleted old binary file: {file_path}")
        return True
    except Exception as e:
        print(f"[WARNING] Error deleting old files: {str(e)}")
        return False

def auto_build_all():
    """Non-interactive mode: Automatically execute full language build (for CI/CD)"""
    print("[INFO] Starting automatic build process for all languages...")
    
    print("\n[STEP 1/4] Skipping clean step (not compatible with Windows CI)")
    # 跳过 make clean 步骤，因为在 Windows CI 环境中会失败
    # 直接进行下一步
    
    print("\n[STEP 2/4] Deleting old binary payloads")
    if not delete_old_bin_files():
        return False

    print("\n[STEP 3/4] Building main program")
    if not run_make_command():
        return False

    print("\n[STEP 4/4] Building all translation files")
    if not run_make_command("translate-all"):
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

def display_menu():
    """Display interactive menu (keep original function for local use)"""
    print("\nSelect an option:")
    print("  1. Build all translations")
    print("  2. Build specific language")
    print("  3. List available languages")
    print("  4. Clean build files")
    print("  5. Exit")
    print()

def build_all_interactive():
    """Interactive mode: Build all translations (local use)"""
    print("\n[INFO] Building main program...")
    if not run_make_command():
        print("[ERROR] Failed to build main program")
        return False
    
    print("\n[INFO] Building all translations...")
    if not delete_old_bin_files(): 
        return False
    if not run_make_command("translate-all"):
        print("[ERROR] Failed to build translations")
        return False
    return True

def build_single():
    """Interactive mode: Build specific language (local use)"""
    print("\n[INFO] Available languages:")
    run_make_command("list-languages")
    
    lang = input("\nEnter language code: ").strip()
    if not lang:
        print("[ERROR] Language code cannot be empty")
        return False
    
    print("\n[INFO] Building main program...")
    if not run_make_command():
        print("[ERROR] Failed to build main program")
        return False
    
    print(f"\n[INFO] Building translation for language: {lang}")
    if not delete_old_bin_files():  
        return False
    if not run_make_command(f"translate-{lang}"):
        print(f"[ERROR] Failed to build translation for {lang}")
        return False
    return True

def list_languages():
    print()
    run_make_command("list-languages")

def clean_build():
    print("\n[INFO] Cleaning build files...")
    run_make_command("clean")
    run_make_command("clean-translate")
    print("[SUCCESS] Clean completed")

def main():
    """Main function: Support both non-interactive (CI) and interactive (local) modes"""
    parser = argparse.ArgumentParser(description="DBI Patcher Translation Builder")
    parser.add_argument(
        "--build-all", 
        action="store_true", 
        help="Non-interactive mode: Automatically build all languages (for CI/CD, no manual input required)"
    )
    args = parser.parse_args()

    print("\n[INFO] DBI Patcher Translation Builder")
    print("====================================")
    
    if not check_make_installed():
        print("[ERROR] make is not installed! Please install first:")
        print("  - Windows: Install via Chocolatey (choco install make)")
        print("  - Linux: sudo apt install make")
        print("  - macOS: brew install make")
        if not args.build_all:  
            input("Press Enter to exit...")
        return
    
    if args.build_all:
        success = auto_build_all()
        sys.exit(0 if success else 1)

    while True:
        display_menu()
        choice = input("Enter your choice (1-5): ").strip()
        
        if choice == "1":
            if build_all_interactive():
                print("\n====================================")
                print("[SUCCESS] Build process completed!")
                print("====================================")
        elif choice == "2":
            if build_single():
                print("\n====================================")
                print("[SUCCESS] Build process completed!")
                print("====================================")
        elif choice == "3":
            list_languages()
        elif choice == "4":
            clean_build()
        elif choice == "5":
            print("Exiting...")
            break
        else:
            print("Invalid choice, please try again.")
        
        if choice in ["1", "2", "3", "4"]:
            input("\nPress Enter to continue...")

if __name__ == "__main__":
    main()
