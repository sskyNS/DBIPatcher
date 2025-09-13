import os
import subprocess
import sys
import shutil
import argparse 

def check_make_installed():
    """检查make是否已安装"""
    try:
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
            text=True,
            shell=True  # 适配Windows环境
        )

        if result.stdout:
            print(f"[MAKE OUTPUT]\n{result.stdout}")
        if result.stderr:
            print(f"[MAKE WARNING]\n{result.stderr}")
        
        if result.returncode != 0:
            print(f"[ERROR] make {target if target else ''} 执行失败")
            return False
        return True
    except Exception as e:
        print(f"[ERROR] 执行make命令时出错: {str(e)}")
        return False

def delete_old_bin_files(target_dir="temp/DBI_810"):
    """删除旧的二进制文件（解决终止序列错误的核心！）
    避免工具误判转换方向（文本→二进制 vs 二进制→文本）
    """
    if not os.path.exists(target_dir):
        print(f"[INFO] 目标目录 {target_dir} 不存在，无需删除旧文件")
        return True
    try:
        for root, _, files in os.walk(target_dir):
            for file in files:
                if file.endswith(".bin"):  # 只删除二进制payload文件
                    file_path = os.path.join(root, file)
                    os.remove(file_path)
                    print(f"[INFO] 已删除旧二进制文件: {file_path}")
        return True
    except Exception as e:
        print(f"[WARNING] 删除旧文件时出错: {str(e)}")
        return False

def auto_build_all():
    """非交互模式：自动执行全语言构建（适配CI/CD）"""
    print("[INFO] 开始自动全语言构建流程...")
    
    print("\n[STEP 1/4] 清理历史构建")
    if not run_make_command("clean"):
        return False
    if not run_make_command("clean-translate"):
        return False
    
    print("\n[STEP 2/4] 删除旧二进制payload")
    if not delete_old_bin_files():
        return False

    print("\n[STEP 3/4] 构建主程序")
    if not run_make_command():
        return False

    print("\n[STEP 4/4] 构建所有翻译文件")
    if not run_make_command("translate-all"):
        return False
    
    trans_output_dir = "temp/DBI_810/bin"
    if not os.path.exists(trans_output_dir):
        print(f"[ERROR] 翻译输出目录 {trans_output_dir} 不存在")
        return False
    trans_files = [f for f in os.listdir(trans_output_dir) if f.startswith("DBI.810.") and f.endswith(".nro")]
    if len(trans_files) == 0:
        print("[ERROR] 未生成任何翻译文件")
        return False
    
    print(f"\n[SUCCESS] 自动构建完成！生成 {len(trans_files)} 个翻译文件：")
    for f in trans_files:
        f_path = os.path.join(trans_output_dir, f)
        f_size = round(os.path.getsize(f_path)/1024, 2)  # 转为KB
        print(f"  - {f} ({f_size} KB)")
    return True

def display_menu():
    """显示交互菜单（保留原功能，供本地使用）"""
    print("\nSelect an option:")
    print("  1. Build all translations")
    print("  2. Build specific language")
    print("  3. List available languages")
    print("  4. Clean build files")
    print("  5. Exit")
    print()

def build_all_interactive():
    """交互模式：构建所有翻译（本地使用）"""
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
    """交互模式：构建特定语言（本地使用）"""
    print("\n[INFO] Available languages:")
    run_make_command("list-languages")
    
    lang = input("\nEnter language code: ").strip()
    if not lang:
        print("[ERROR] 语言代码不能为空")
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
    """主函数：支持非交互（CI）和交互（本地）模式"""
    parser = argparse.ArgumentParser(description="DBI Patcher Translation Builder")
    parser.add_argument(
        "--build-all", 
        action="store_true", 
        help="非交互模式：自动构建所有语言（适配CI/CD，无需手动输入）"
    )
    args = parser.parse_args()

    print("\n[INFO] DBI Patcher Translation Builder")
    print("====================================")
    
    if not check_make_installed():
        print("[ERROR] make 未安装！请先安装：")
        print("  - Windows: 通过 Chocolatey 安装（choco install make）")
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
