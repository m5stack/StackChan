import os
import re
import sys

# ==========================================
# Configuration / 配置
# ==========================================

# Directories to exclude from scanning
# 不需要扫描的目录
EXCLUDE_DIRS = {
    '.git', 
    '.idea', 
    '.vscode', 
    '__pycache__', 
}

def load_gitignore():
    """Load exclusions from .gitignore / 从 .gitignore 加载排除项"""
    if os.path.exists('.gitignore'):
        print("Loading .gitignore...")
        with open('.gitignore', 'r') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('#'): continue
                
                # Normalize path: remove leading/trailing slashes
                # 简单处理：移除开头结尾的斜杠
                clean_item = line.rstrip('/').lstrip('/')
                
                # Skip patterns with wildcards (simple directory names only)
                # 跳过带通配符的复杂规则，仅添加确定的目录名
                if '*' not in clean_item:
                    EXCLUDE_DIRS.add(clean_item)

# Load gitignore patterns
load_gitignore()

# File extensions to exclude (binary files, images, etc.)
# 不需要扫描的文件类型
EXCLUDE_EXTENSIONS = {
    '.o', '.a', '.elf', '.bin', '.map', '.hex', 
    '.png', '.jpg', '.jpeg', '.gif', '.bmp', '.ico', 
    '.wav', '.mp3', '.ogg', 
    '.pdf', '.zip', '.tar', '.gz', 
    '.pyc', '.ninja'
}

# Regular expressions for sensitive data
# 敏感信息的正则表达式匹配规则
PATTERNS = [
    # 1. IP Addresses (Exclude localhost and common local IPs)
    # 匹配可能的硬编码公网 IP (排除 127.0.0.1, 0.0.0.0, 192.168.x.x)
    (r'\b(?!127\.0\.0\.1|0\.0\.0\.0|192\.168\.)\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b', "Potential Public IP"),
    
    # 2. Key/Secret Assignments
    # 匹配变量赋值: 变量名包含 key/secret/token/password 等
    # regex matches: variable_name = "value" OR variable_name: "value"
    (r'(?i)(api[_-]?key|secret|token|password|passwd|pwd|auth|access[_-]?key|client[_-]?secret|private[_-]?key)\s*[:=]\s*["\']([^"\']+)["\']', "Potential Secret/Key"),
    
    # 3. WiFi Credentials
    # 匹配 WiFi SSID 或 Password
    (r'(?i)(ssid|wifi[_-]?pass(word)?)\s*[:=]\s*["\']([^"\']+)["\']', "Potential WiFi Credential"),
    
    # 4. URLs with Credentials
    # 匹配 http://user:pass@host 格式
    (r'https?://[^:\s]+:[^@\s]+@[^/\s]+', "URL with Credentials"),
    
    # 5. Cryptographic Key Headers (Private/Public)
    # 匹配 RSA/DSA 私钥或公钥头
    (r'-----BEGIN\s+[A-Z\s]+KEY-----', "Cryptographic Key Block"),
    
    # 6. AWS Access Key ID (Common Pattern)
    (r'\bAKIA[0-9A-Z]{16}\b', "AWS Access Key ID"),

    # 7. Generic high-entropy strings (simplified view for Bearer tokens etc)
    # 匹配 "Bearer <token>" 格式
    (r'Bearer\s+[a-zA-Z0-9\-\._~\+/]{20,}', "Potential Bearer Token"),
]

# Allow-list for dummy values (Common placeholders to ignore)
# 白名单：如果是这些值，则不认为是泄露
ALLOW_LIST = {
    "", 
    "your_ssid", "your_password", "password", "12345678", "00000000",
    "dummy", "example", "changeme", "TODO", "x" * 10
}

# ==========================================
# Script Logic / 逻辑实现
# ==========================================

def is_text_file(filepath):
    """Check if file is text by reading first chunk."""
    try:
        with open(filepath, 'rb') as f:
            chunk = f.read(1024)
        if not chunk: return True # Empty file
        if b'\0' in chunk: return False # Contains null bytes -> binary
        return True
    except Exception:
        return False

def check_line(line, line_num):
    issues = []
    for pattern, desc in PATTERNS:
        matches = re.finditer(pattern, line)
        for match in matches:
            matched_text = match.group(0)
            
            # If pattern has groups (like variable assignment), check the value part
            if len(match.groups()) >= 2 and match.group(2):
                sensitive_val = match.group(2)
                if sensitive_val in ALLOW_LIST:
                    continue
                # Simple heuristc: skip if looks like a format string placeholder
                if "%s" in sensitive_val or "%d" in sensitive_val or "{}" in sensitive_val:
                    continue

            # Skip common false positives for IP
            if desc == "Potential Public IP":
                # Skip version numbers that look like IPs (e.g. 1.0.0.0 in cmake)
                if "version" in line.lower():
                    continue

            issues.append({
                'desc': desc,
                'match': matched_text
            })
    return issues

def scan_files(root_dir):
    print(f"Scanning directory: {root_dir}")
    print(f"Ignoring directories: {', '.join(EXCLUDE_DIRS)}")
    print("-" * 60)
    
    found_issues_count = 0
    
    for dirpath, dirnames, filenames in os.walk(root_dir):
        # Filter directories
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
        
        for filename in filenames:
            # Filter extensions
            _, ext = os.path.splitext(filename)
            if ext.lower() in EXCLUDE_EXTENSIONS:
                continue
            
            # Skip self
            if filename == os.path.basename(__file__):
                continue
                
            filepath = os.path.join(dirpath, filename)
            
            if not is_text_file(filepath):
                continue
                
            try:
                with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                    file_issues = []
                    for i, line in enumerate(f, 1):
                        line_stripped = line.strip()
                        if not line_stripped: continue
                        
                        line_issues = check_line(line_stripped, i)
                        if line_issues:
                            for issue in line_issues:
                                file_issues.append((i, issue['desc'], issue['match'], line_stripped))
                    
                    if file_issues:
                        found_issues_count += 1
                        print(f"\n[FILE] {os.path.relpath(filepath, root_dir)}")
                        for line_num, desc, match, content in file_issues:
                            # Truncate content for display
                            disp_content = content[:80] + "..." if len(content) > 80 else content
                            print(f"  Line {line_num}: \033[91m{desc}\033[0m")
                            print(f"    Match: {match}")
                            print(f"    Code : {disp_content}")
                            
            except Exception as e:
                print(f"[WARN] Could not read {filepath}: {e}")

    print("-" * 60)
    if found_issues_count == 0:
        print("\033[92mNo obvious sensitive hardcoded strings found.\033[0m")
    else:
        print(f"\033[93mScan complete. Found potential issues in {found_issues_count} files.\033[0m")
        print("Please review them manually to ensure no real secrets are leaked.")

if __name__ == "__main__":
    scan_files(os.getcwd())
