import os
import sys
import subprocess
import logging

# Set up basic logging
logging.basicConfig(level=logging.INFO, format='[AzureSign] %(message)s')

# --- Configuration Constants (Azure Key Vault) ---
# Tool name (must be in PATH or provide full path)
AZURE_SIGN_TOOL = "azuresigntool"

# Credentials provided
KEY_VAULT_URL = "https://installer-keys.vault.azure.net/"
CLIENT_ID     = "3795b036-ead2-433e-b22a-83cd32c7c625"
CLIENT_SECRET = "dummy"
TENANT_ID     = "acc04609-dd29-4b33-8be4-7a7f58722f24"
CERT_NAME     = "JatterInstallerCertificate"
TIMESTAMP_URL = "http://timestamp.digicert.com"
# -------------------------------------------------

def sign_batch(files_to_sign):
    """Signs a list of files in a single AzureSignTool transaction."""
    
    # Filter out files that don't exist to prevent the tool from crashing
    existing_files = []
    for f in files_to_sign:
        if os.path.exists(f):
            existing_files.append(f)
        else:
            logging.warning(f"File missing, skipping: {f}")

    if not existing_files:
        logging.warning("No valid files to sign.")
        return

    logging.info(f"Batch signing {len(existing_files)} files with Azure Key Vault...")

    # Construct the command
    # azuresigntool sign -kvu ... -kvi ... -tr ... -v file1 file2 file3
    cmd = [
        AZURE_SIGN_TOOL, "sign",
        "-kvu", KEY_VAULT_URL,
        "-kvi", CLIENT_ID,
        "-kvs", CLIENT_SECRET,
        "-kvt", TENANT_ID,
        "-kvc", CERT_NAME,
        "-tr", TIMESTAMP_URL,
        "-td", "sha256",
        "-v" # Verbose output is helpful for CI logs
    ]
    
    # Append all file paths to the end of the command
    cmd.extend(existing_files)

    try:
        # Run the command. We capture output to print it nicely in the build logs.
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        logging.info("AzureSignTool Output:\n" + result.stdout)
        logging.info("Batch signing successful.")
    except subprocess.CalledProcessError as e:
        logging.error("AzureSignTool Failed.")
        logging.error("STDOUT:\n" + e.stdout)
        logging.error("STDERR:\n" + e.stderr)
        sys.exit(1)

def main(build_dir, files_to_sign):
    # Resolve full paths for all binaries
    full_paths = [os.path.join(build_dir, f) for f in files_to_sign]
    sign_batch(full_paths)

if __name__ == "__main__":
    # GN arguments:
    # argv[1] = build_dir
    # argv[2] = stamp_file_path
    # argv[3+] = files_to_sign
    
    if len(sys.argv) < 3:
        logging.error("Usage: sign.py <build_dir> <stamp_file> <file1> ...")
        sys.exit(1)

    build_dir = os.path.abspath(sys.argv[1])
    stamp_file = sys.argv[2]
    files_to_sign = sys.argv[3:]

    main(build_dir, files_to_sign)

    # Create the stamp file to satisfy GN dependencies
    with open(stamp_file, 'w') as f:
        f.write("Signed with Azure")