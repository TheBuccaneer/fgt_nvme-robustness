# Tooling / Setup (NVMe-lite / B-plus)

Dieses Dokument sammelt **alle Tools/Software**, die du für Build, Runs und Auswertung brauchst – inkl. Install-Befehlen.

## 0) Annahmen
- Linux (getestet/gedacht für **Ubuntu/Debian**).  
  Wenn du Fedora/Arch nutzt: sag kurz Bescheid, dann gebe ich dir die äquivalenten Paketnamen.

---

## 1) Basis-Pakete (Build + Utils)

```bash
sudo apt update
sudo apt install -y build-essential git pkg-config cmake ninja-build
sudo apt install -y python3 python3-venv python3-pip
sudo apt install -y jq unzip zip
```

Optional aber praktisch:
```bash
sudo apt install -y time parallel
sudo apt install -y htop
```

---

## 2) C/C++ Toolchain (für DUT / Sanitizer / libFuzzer)

Empfohlen: Clang/LLVM (für Sanitizer + libFuzzer).

```bash
sudo apt install -y clang llvm lld
```

Optional (Debugging):
```bash
sudo apt install -y gdb
sudo apt install -y valgrind
```

Hinweis:
- ASAN/UBSAN laufen mit Clang direkt über Compiler-Flags (keine Extra-Installation nötig, wenn clang da ist).

---

## 3) Rust Toolchain (für Oracle / Referenz)

### 3.1 rustup + stable
```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
```

Shell neu laden (oder neue Session):
```bash
source "$HOME/.cargo/env"
```

Stable toolchain:
```bash
rustup toolchain install stable
rustup default stable
```

Optional (Format/Lint):
```bash
rustup component add rustfmt
rustup component add clippy
```

---

## 4) Python-Umgebung (Plots + CSV-Pipeline)

Im Repo eine venv anlegen:

```bash
python3 -m venv .venv
```

Aktivieren:
```bash
source .venv/bin/activate
```

Pip updaten:
```bash
python -m pip install --upgrade pip
```

Pakete (für CSV + Plots):
```bash
pip install numpy pandas matplotlib
```

Optional (CLI-Komfort):
```bash
pip install rich
```

Empfehlung fürs Repo:
- `requirements.txt` anlegen und pinnen (mindestens grob), z.B.:
  - numpy
  - pandas
  - matplotlib
  - rich (optional)

---

## 5) Optional: Fuzzing-/Test-Extras (nur wenn ihr sie wirklich nutzt)

### 5.1 cargo-fuzz (nur falls Rust-Fuzzing gebraucht wird)
```bash
cargo install cargo-fuzz
```

### 5.2 afl++ (nur wenn ihr AFL statt libFuzzer wollt)
```bash
sudo apt install -y afl++
```

---

## 6) Projekt-Checks (schnell prüfen, ob alles da ist)

```bash
git --version
```

```bash
clang --version
```

```bash
rustc --version
```

```bash
python3 --version
```

```bash
python -c "import numpy, pandas, matplotlib; print('python deps ok')"
```

---

## 7) Was davon ist wirklich *Pflicht*?

**Pflicht (Minimum):**
- git, cmake/ninja, build-essential
- clang/llvm
- rustup (stable)
- python3 + venv + numpy/pandas/matplotlib

**Optional:**
- gdb/valgrind (Debugging)
- time/parallel (Runs bequemer)
- cargo-fuzz / afl++ (nur wenn ihr diese Engines wirklich fahrt)
