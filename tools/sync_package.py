"""Refresh website/package source files. Does not compile or include game data."""
from pathlib import Path
import shutil
ROOT = Path(__file__).resolve().parents[1]
def sync():
    package = ROOT / "package"
    data = package / "realracing3"
    data.mkdir(parents=True, exist_ok=True)
    for name in ["port.json", "README.md", "gameinfo.xml", "cover.png", "screenshot.png"]:
        shutil.copyfile(ROOT / "ports/realracing3" / name, package / name)
    shutil.copyfile(ROOT / "ports/Real Racing 3.sh", package / "Real Racing 3.sh")
    for name in ["realracing3.ini", "realracing3.eapx.json", "CREDITS.md", "PUT_REAL_RACING_3_DATA_HERE.txt"]:
        shutil.copyfile(ROOT / "ports/realracing3" / name, data / name)
    shutil.copyfile(ROOT / "tools/eapx.py", data / "eapx.py")
    licenses = data / "licenses"
    licenses.mkdir(exist_ok=True)
    for origin, name in [("LICENSE", "LICENSE-portmaster-port.txt"), ("LICENSE", "LICENSE-eapx.txt"),
                         ("NOTICE.md", "NOTICE.md"), ("ports/realracing3/LICENSE-gptokeyb.txt", "LICENSE-gptokeyb.txt")]:
        shutil.copyfile(ROOT / origin, licenses / name)
    shutil.copyfile(ROOT / "testing_thread.txt", package / "testing_thread.txt")
    shutil.copyfile(ROOT / 'LICENSE', licenses / 'LICENSE-portmaster-port.txt')
    shutil.copyfile(ROOT / 'LICENSE', licenses / 'LICENSE-eapx.txt')
    shutil.copyfile(ROOT / 'NOTICE.md', licenses / 'NOTICE.md')
    shutil.copyfile(ROOT / 'third_party/gmloader/LICENSE.md', licenses / 'LICENSE-gmloader.md')
    shutil.copyfile(ROOT / 'third_party/powervr/LICENSE.md', licenses / 'LICENSE-powervr.txt')
    shutil.copyfile(ROOT / 'third_party/stb/LICENSE.md', licenses / 'LICENSE-stb.md')
if __name__ == "__main__":
    sync()
    print("Updated package/ website metadata and redistributable source files.")
