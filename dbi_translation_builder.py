import os
import subprocess
import sys

def check_make_installed():
    """检查make是否已安装"""
    try:
        # 尝试运行make --version命令
        subprocess.run(
            ["make", "--version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        return True
    except FileNotFoundError:
        return False

def run_make_command(target=None):
    """运行make命令，可指定目标"""
    try:
        args = ["make", "--no-print-directory"]
        if target:
            args.append(target)
            
        result = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # 输出make命令的输出
        if result.stdout:
            print(result.stdout)
        
        # 检查是否有错误
        if result.returncode != 0:
            print(f"[ERROR] make命令执行失败: {result.stderr}")
            return False
        return True
    except Exception as e:
        print(f"[ERROR] 执行make命令时出错: {str(e)}")
        return False

def display_menu():
    """显示菜单选项"""
    print("\nSelect an option:")
    print("  1. Build all translations")
    print("  2. Build specific language")
    print("  3. List available languages")
    print("  4. Clean build files")
    print("  5. Exit")
    print()

def build_all():
    """构建所有翻译"""
    print("\n[INFO] Building main program...")
    if not run_make_command():
        print("[ERROR] Failed to build main program")
        return False
    
    print("\n[INFO] Building all translations...")
    if not run_make_command("translate-all"):
        print("[ERROR] Failed to build translations")
        return False
    
    return True

def build_single():
    """构建特定语言的翻译"""
    print("\n[INFO] Available languages:")
    run_make_command("list-languages")
    
    lang = input("\nEnter language code: ").strip()
    
    print("\n[INFO] Building main program...")
    if not run_make_command():
        print("[ERROR] Failed to build main program")
        return False
    
    print(f"\n[INFO] Building translation for language: {lang}")
    if not run_make_command(f"translate-{lang}"):
        print(f"[ERROR] Failed to build translation for {lang}")
        return False
    
    return True

def list_languages():
    """列出可用语言"""
    print()
    run_make_command("list-languages")

def clean_build():
    """清理构建文件"""
    print("\n[INFO] Cleaning build files...")
    run_make_command("clean")
    run_make_command("clean-translate")
    print("[SUCCESS] Clean completed")

def main():
    """主函数"""
    print("\n[INFO] DBI Patcher Translation Builder")
    print("====================================")
    
    # 检查make是否安装
    if not check_make_installed():
        print("[ERROR] make is not installed. Please install make first.")
        print("        Install make from Cygwin or MinGW")
        input("Press Enter to exit...")
        return
    
    while True:
        display_menu()
        choice = input("Enter your choice (1-5): ").strip()
        
        if choice == "1":
            if build_all():
                print("\n====================================")
                print("[SUCCESS] Build process completed!")
                print("====================================")
                input("\nPress Enter to continue...")
                
        elif choice == "2":
            if build_single():
                print("\n====================================")
                print("[SUCCESS] Build process completed!")
                print("====================================")
                input("\nPress Enter to continue...")
                
        elif choice == "3":
            list_languages()
            input("\nPress Enter to continue...")
            
        elif choice == "4":
            clean_build()
            input("\nPress Enter to continue...")
            
        elif choice == "5":
            print("Exiting...")
            break
            
        else:
            print("Invalid choice, please try again.")

if __name__ == "__main__":
    main()
