import argparse
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--tagger', required=True, help='Path to tag.exe')
    parser.add_argument('--in-file', required=True, help='Input signed EXE')
    parser.add_argument('--out-file', required=True, help='Output tagged EXE')
    parser.add_argument('--tag-string', required=True, help='The Omaha tag payload')
    args = parser.parse_args()

    # Construct and run the tag.exe command
    cmd = [
        args.tagger,
        f"--set-tag={args.tag_string}",
        f"--out={args.out_file}",
        args.in_file
    ]
    
    print(f"Tagging installer: {' '.join(cmd)}")
    result = subprocess.run(cmd)
    sys.exit(result.returncode)

if __name__ == '__main__':
    main()