#!/usr/bin/env python3
"""
Installation script for MeshNet systemd service
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path


def check_root():
    """Check if script is run as root"""
    if os.geteuid() != 0:
        print("❌ This script must be run as root (use sudo)")
        sys.exit(1)


def create_user():
    """Create meshnet user if it doesn't exist"""
    try:
        subprocess.run(['id', 'meshnet'], check=True, capture_output=True)
        print("✓ User 'meshnet' already exists")
    except subprocess.CalledProcessError:
        print("Creating user 'meshnet'...")
        subprocess.run([
            'useradd',
            '--system',
            '--no-create-home',
            '--shell', '/bin/false',
            'meshnet'
        ], check=True)
        print("✓ User 'meshnet' created")


def install_files():
    """Install MeshNet to /opt/meshnet"""
    install_dir = Path('/opt/meshnet')
    current_dir = Path.cwd()

    print(f"Installing MeshNet to {install_dir}...")

    # Create installation directory
    install_dir.mkdir(parents=True, exist_ok=True)

    # Copy files
    files_to_copy = [
        'meshnet.py',
        'requirements.txt',
        'config.example.json',
        'backend',
    ]

    for item in files_to_copy:
        src = current_dir / item
        dst = install_dir / item

        if src.is_file():
            shutil.copy2(src, dst)
            print(f"✓ Copied {item}")
        elif src.is_dir():
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
            print(f"✓ Copied {item}/")

    # Copy frontend dist if it exists
    frontend_dist = current_dir / 'frontend' / 'dist'
    if frontend_dist.exists():
        dst = install_dir / 'frontend' / 'dist'
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(frontend_dist, dst)
        print("✓ Copied frontend/dist/")
    else:
        print("⚠️  Frontend not built. Run 'cd frontend && npm run build' first for production mode.")

    # Create directories
    for dir_name in ['data', 'logs']:
        (install_dir / dir_name).mkdir(exist_ok=True)

    # Create config if it doesn't exist
    config_path = install_dir / 'config.json'
    if not config_path.exists():
        shutil.copy2(install_dir / 'config.example.json', config_path)
        print("✓ Created config.json from example")

    # Set permissions
    subprocess.run(['chown', '-R', 'meshnet:meshnet', str(install_dir)], check=True)
    subprocess.run(['chmod', '+x', str(install_dir / 'meshnet.py')], check=True)

    print("✓ Files installed")


def install_service():
    """Install systemd service"""
    service_file = Path.cwd() / 'meshnet.service'
    service_dest = Path('/etc/systemd/system/meshnet.service')

    print("Installing systemd service...")

    # Copy service file
    shutil.copy2(service_file, service_dest)

    # Reload systemd
    subprocess.run(['systemctl', 'daemon-reload'], check=True)

    print("✓ Service installed")
    print("\nTo start the service:")
    print("  sudo systemctl start meshnet")
    print("\nTo enable auto-start on boot:")
    print("  sudo systemctl enable meshnet")
    print("\nTo check status:")
    print("  sudo systemctl status meshnet")
    print("\nTo view logs:")
    print("  sudo journalctl -u meshnet -f")


def install_dependencies():
    """Install Python dependencies"""
    print("Installing Python dependencies...")
    subprocess.run([
        sys.executable,
        '-m',
        'pip',
        'install',
        '-r',
        '/opt/meshnet/requirements.txt'
    ], check=True)
    print("✓ Dependencies installed")


def main():
    print("=" * 60)
    print("MeshNet - Reticulum Network System")
    print("Service Installation Script")
    print("=" * 60)
    print()

    check_root()
    create_user()
    install_files()
    install_dependencies()
    install_service()

    print()
    print("=" * 60)
    print("✓ Installation complete!")
    print("=" * 60)
    print()
    print("Next steps:")
    print("1. Edit /opt/meshnet/config.json with your settings")
    print("2. Configure Reticulum in /home/meshnet/.reticulum/")
    print("3. Start the service: sudo systemctl start meshnet")
    print("4. Enable auto-start: sudo systemctl enable meshnet")
    print()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n❌ Installation cancelled")
        sys.exit(1)
    except Exception as e:
        print(f"\n\n❌ Installation failed: {e}")
        sys.exit(1)
