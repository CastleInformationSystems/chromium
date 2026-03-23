import os
import sys
import subprocess
import logging

logging.basicConfig(level=logging.INFO, format='[AzureSignWrapper] %(message)s')

# --- Configuration Constants (Azure Key Vault) ---
AZURE_SIGN_TOOL = "azuresigntool"

KEY_VAULT_URL = "https://installer-keys.vault.azure.net/"
CLIENT_ID     = "3795b036-ead2-433e-b22a-83cd32c7c625"
CLIENT_SECRET = "dummy"
TENANT_ID     = "acc04609-dd29-4b33-8be4-7a7f58722f24"
CERT_NAME     = "JatterInstallerCertificate"
TIMESTAMP_URL = "http://timestamp.digicert.com"
# -------------------------------------------------

def sign_file(file_path):
    if not os.path.exists(file_path):
        logging.error(f"Installer not found: {file_path}")
        sys.exit(1)

    logging.info(f"Signing installer with Azure: {os.path.basename(file_path)}")

    cmd = [
        AZURE_SIGN_TOOL, "sign",
        "-kvu", KEY_VAULT_URL,
        "-kvi", CLIENT_ID,
        "-kvs", CLIENT_SECRET,
        "-kvt", TENANT_ID,
        "-kvc", CERT_NAME,
        "-tr", TIMESTAMP_URL,
        "-td", "sha256",
        "-v",
        file_path
    ]

    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        logging.info("Successfully signed mini_installer.exe")
    except subprocess.CalledProcessError as e:
        logging.error("Failed to sign mini_installer.exe")
        logging.error(e.stdout)
        logging.error(e.stderr)
        sys.exit(1)

if __name__ == "__main__":
    # GN arguments:
    # argv[1] = installer_path
    # argv[2] = stamp_file_path

    if len(sys.argv) < 3:
        logging.error("Usage: sign_mini_installer.py <installer_path> <stamp_file>")
        sys.exit(1)

    installer_path = os.path.abspath(sys.argv[1])
    stamp_file = sys.argv[2]

    sign_file(installer_path)

    with open(stamp_file, 'w') as f:
        f.write("Signed with Azure")