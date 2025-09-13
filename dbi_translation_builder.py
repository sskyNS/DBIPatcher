import os
import subprocess
import sys
import shutil
import argparse 
import io

# 修复Windows环境下的编码问题
def setup_encoding():
    """设置正确的编码以避免Unicode错误"""
    if sys.platform == "win32":
        # Windows系统设置UTF-8编码
        if hasattr(sys.stdout, 'buffer'):
            sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
        if hasattr(sys.stderr, 'buffer'):
            sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')
        
        # 设置环境变量
        os.environ['PYTHONIOENCODING'] = 'utf-8'

# 在脚本开始时调用
setup_encoding()

def check_make_installed():
    """检查make是否已安装"""
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
            encoding='utf-8',
            shell=True
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

def auto_build_all():
    """非交互模式：自动执行全语言构建（适配CI/CD）"""
    print("[INFO] Starting automatic build process for all languages...")
    
    print("\n[STEP 1/4] Cleaning previous builds")
    if not run_make_command("clean"):
        return False
    if not run_make_command("clean-translate"):
        return False
    
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

# 其余函数保持不变...
